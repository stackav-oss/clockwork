// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/channel_type_clk_cc.hh"
#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/message_encoding_clk_cc.hh"
#include "clockwork/logging/onboard/clockwork_writer_policy.hh"
#include "clockwork/logging/onboard/null_message_handle.hh"
#include "clockwork/logging/onboard/types.hh"
#include "clockwork/logging/onboard/writer.hh"
#include "clockwork/logging/schema_encoding_clk_cc.hh"
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/buffer_index.hh"
#include "clockwork/pinion/buffer_layout.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/slot_ref.hh"
#include "jewels/math/constants.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/shared_pool/ref_counted_pool.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"

#include <fmt/format.h>
#include <tclap/ArgException.h>
#include <tclap/CmdLine.h>
#include <tclap/ValueArg.h>
#include <wise_enum.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace clockwork_logging::tools
{

/// Test log directory name
constexpr auto log_dir_name = "log_perf";

/// Log file prefix
constexpr auto log_file_prefix = "test";

/// Test channel name
constexpr auto channel_name = "test_channel";

/// Test schema name
constexpr auto schema_name = "test_schema";

/// Duration to sleep waiting for discards after deleting the test output directory
constexpr auto discard_sleep_duration = std::chrono::seconds(10);

/// Writer periodic callback interval
constexpr auto periodic_callback_interval = std::chrono::milliseconds(5);

/// Max log file duration. Zero indicates infinite.
constexpr auto max_log_file_duration = std::chrono::seconds{0};

/// Aligned buffer pointer
using AlignedBufferPointerType = std::unique_ptr<std::span<std::byte>, std::function<void(std::span<std::byte>*)>>;

/// Allocate an aligned buffer
/// @param[in] memory_resource Memory resource
/// @param[in] buffer_size Buffer size
/// @param[in] buffer_alignment
/// @return Unique pointer to span containing the aligned buffer
[[nodiscard]] AlignedBufferPointerType
make_aligned_buffer(jewels::memory::MemoryResource memory_resource, size_t buffer_size, size_t alignment)
{
  auto* memory_resource_ptr = static_cast<std::pmr::memory_resource*>(memory_resource);
  auto* buffer_data_ptr = static_cast<std::byte*>(memory_resource_ptr->allocate(buffer_size, alignment));
  auto* buffer_span_ptr =
    static_cast<std::span<std::byte>*>(memory_resource_ptr->allocate(sizeof(std::span<std::byte>)));
  *buffer_span_ptr = std::span{buffer_data_ptr, buffer_size};
  return AlignedBufferPointerType{
    buffer_span_ptr,
    [memory_resource_ptr, alignment](std::span<std::byte>* buffer_span_ptr)
    {
      memory_resource_ptr->deallocate(buffer_span_ptr->data(), buffer_span_ptr->size(), alignment);
      memory_resource_ptr->deallocate(buffer_span_ptr, sizeof(std::span<std::byte>));
    }};
}

/// Make a pinion buffer layout for a given message size and data rate
/// @param[in] message_size Message size
/// @param[in] rate_mib_per_sec Data rate in MiB/sec
/// @return Pinion buffer layout with enough messages for the desired rate
[[nodiscard]] clockwork::pinion::BufferLayout make_buffer_layout(size_t message_size, uint64_t rate_mib_per_sec)
{
  const auto max_backlog_sec =
    std::chrono::duration_cast<std::chrono::duration<double>>(onboard::ClockworkWriterPolicy<>::max_write_backlog);
  const auto num_slots = static_cast<size_t>(std::ceil(
    static_cast<double>(rate_mib_per_sec) * jewels::math::constants::bytes_per_mib<double> * max_backlog_sec.count() /
    static_cast<double>(message_size)));
  return clockwork::pinion::BufferLayout{
    .num_slots = num_slots,
    .message_size = message_size,
    .is_published_once = false,
  };
}

/// Fill a buffer with random data
/// @param[in] buffer Buffer to fill
/// @param[in] zero_size Number of bytes to set to zero
void fill_with_random_bytes(std::span<std::byte> buffer, size_t zero_size)
{
  memset(buffer.data(), 0, buffer.size());
  std::random_device random_device;
  std::mt19937 gen(random_device());
  std::uniform_int_distribution<uint8_t> distrib(
    std::numeric_limits<uint8_t>::min(), std::numeric_limits<uint8_t>::max());
  for (auto& elem : buffer.first(buffer.size() - zero_size))
  {
    elem = std::byte{distrib(gen)};
  }
}

/// Counters used to measure I/O performance
struct PerfCounters
{
  /// Measurement start time
  jewels::time::SteadyTime start_time{};

  /// Measurement end time
  jewels::time::SteadyTime end_time{};

  /// Number of bytes written
  uint64_t byte_count{};

  /// Number of writes
  uint64_t write_count{};

  /// Maximum write backlog
  std::chrono::duration<double, std::chrono::milliseconds::period> max_backlog{};

  /// Number of dropped messages
  uint64_t drop_count{};

  /// Print the header for the performance counters
  static void print_header();

  /// Print the performance counters
  void print() const;
};

void PerfCounters::print_header()
{
  std::cout << "  Time  DataRate MaxBacklog   Writes Drops\n";
  std::cout << " (Sec) (MiB/sec)   (msec)      (hz)  (cnt)\n" << std::flush;
}

void PerfCounters::print() const
{
  std::cout << fmt::format(
    "{:6.1f} ", std::chrono::duration<double, std::chrono::seconds::period>(start_time.time_since_epoch()).count());
  const std::chrono::duration<double, std::chrono::seconds::period> duration = end_time - start_time;
  if (duration < std::chrono::milliseconds(1))
  {
    std::cout << "SHORT INTERVAL!!!\n" << std::flush;
    return;
  }
  const auto mib_per_sec =
    static_cast<double>(byte_count) / jewels::math::constants::bytes_per_mib<double> / duration.count();
  std::cout << fmt::format("{:9.2f} {:10.1f} {:8d} {:5d}", mib_per_sec, max_backlog.count(), write_count, drop_count);
  if (drop_count != 0U)
  {
    std::cout << " !!!";
  }
  std::cout << '\n' << std::flush;
}

/// Log performance tool implementation
///
/// This class is  designed to be run in a multi-threaded environment, where a test thread is submitting
/// asynchronous I/O requests that are executed by a thread servicing an asynchronous work queue.
/// Mutexes are used to synchronize access to the test performance counters and the list of pending
/// write requests.
template <typename MessageHandleType>
class LogPerfImpl
{
public:
  /// Constructor
  /// @param[in] test_time Total test time
  /// @param[in] message_size Message size in bytes
  /// @param[in] zero_size Number of message bytes to set to zero
  /// @param[in] write_mib_per_sec Write rate in MiB per second
  /// @param[in] output_dir Test output directory
  LogPerfImpl(
    std::chrono::nanoseconds test_time,
    uint64_t message_size,
    uint64_t zero_size,
    uint64_t write_mib_per_sec,
    std::string output_dir);

  ~LogPerfImpl() = default;

  LogPerfImpl(const LogPerfImpl&) = delete;
  LogPerfImpl& operator=(const LogPerfImpl&) = delete;
  LogPerfImpl(LogPerfImpl&&) = delete;
  LogPerfImpl& operator=(LogPerfImpl&&) = delete;

  /// Run the test
  /// @return MonoError on failure
  [[nodiscard]] jewels::expected<void, jewels::MonoError> run();

  /// Test thread function
  void test_thread_fn();

  /// Struct used to pack the performance counter state together
  struct PerfCounterState
  {
    /// Mutex protecting the performance counters
    std::mutex mutex;

    /// Number of dropped messages
    size_t drop_count{};

    /// Maximum measured backlog
    std::chrono::duration<double, std::chrono::milliseconds::period> max_backlog{};

    /// Performance counters pointer
    std::unique_ptr<PerfCounters> pointer;
  };

  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;

  /// Performance counter state
  std::unique_ptr<PerfCounterState> perf_counter_state_ptr_;

  /// Total test time
  std::chrono::nanoseconds test_time_;

  /// Message size in bytes
  uint64_t message_size_;

  /// Number of message bytes to set to zero
  uint64_t zero_size_;

  /// Write interval
  std::chrono::duration<double, std::chrono::seconds::period> write_interval_;

  /// Test thread state
  struct ThreadState
  {
    ThreadState() = default;
    ~ThreadState() = default;

    ThreadState(const ThreadState&) = delete;
    ThreadState& operator=(const ThreadState&) = delete;
    ThreadState(ThreadState&&) = delete;
    ThreadState& operator=(ThreadState&&) = delete;

    /// Shutdown requested flag
    std::atomic<bool> shutdown_flag{false};
  };

  /// Worker thread state
  std::unique_ptr<ThreadState> thread_state_ptr_;

  /// Test thread
  std::thread test_thread_;

  /// Pinion buffer layout
  clockwork::pinion::BufferLayout buffer_layout_;

  /// Clockwork buffer storage pointer
  AlignedBufferPointerType buffer_storage_ptr_;

  /// Pinion buffer
  clockwork::pinion::Buffer pinion_buffer_;

  /// Pinion buffer index
  clockwork::pinion::BufferIndex pinion_buffer_index_{0U};

  /// Test output directory
  std::string output_dir_;

  /// Log writer
  onboard::Writer<onboard::ClockworkWriterPolicy<>> writer_;
};

template <typename MessageHandleType>
LogPerfImpl<MessageHandleType>::LogPerfImpl(
  std::chrono::nanoseconds test_time,
  uint64_t message_size,
  uint64_t zero_size,
  uint64_t write_mib_per_sec,
  std::string output_dir)
  : memory_resource_(std::pmr::new_delete_resource()),
    perf_counter_state_ptr_(std::make_unique<PerfCounterState>()),
    test_time_(test_time),
    message_size_(message_size),
    zero_size_(std::min(message_size, zero_size)),
    write_interval_(
      std::chrono::duration<double, std::chrono::seconds::period>(
        static_cast<double>(message_size) / static_cast<double>(write_mib_per_sec) /
        jewels::math::constants::bytes_per_mib<double>)),
    buffer_layout_(make_buffer_layout(message_size, write_mib_per_sec)),
    buffer_storage_ptr_(make_aligned_buffer(
      memory_resource_, clockwork::pinion::buffer_size(buffer_layout_), clockwork::pinion::Slot::slot_alignment)),
    pinion_buffer_(clockwork::pinion::Buffer::try_make(*buffer_storage_ptr_, buffer_layout_).value()),
    output_dir_(std::move(output_dir)),
    writer_(
      memory_resource_, memory_resource_, write_mib_per_sec, max_log_file_duration, onboard::WriterEnvironment::normal)
{
  while (pinion_buffer_index_ < buffer_layout_.num_slots)
  {
    auto pinion_buffer_iterator = std::end(pinion_buffer_);
    auto pinion_slot = pinion_buffer_iterator.dereference();
    fill_with_random_bytes(pinion_slot.message(), zero_size_);
    if (const auto inc_result = pinion_buffer_.increment_head(pinion_buffer_index_, 1UL); !inc_result)
    {
      throw std::runtime_error("Failed to increment the pinion buffer head");
    }
    ++pinion_buffer_index_;
  }
}

template <typename MessageHandleType>
[[nodiscard]] jewels::expected<void, jewels::MonoError> LogPerfImpl<MessageHandleType>::run()
{
  std::filesystem::create_directories(output_dir_);
  const auto log_dir_path = std::filesystem::path{output_dir_} / log_dir_name;
  if (std::filesystem::exists(log_dir_path))
  {
    std::cout << "Deleting test output directory..." << std::flush;
    std::filesystem::remove_all(log_dir_path);
    std::cout << "done\n";
    std::cout << "Sleeping to wait for discards to complete..." << std::flush;
    std::this_thread::sleep_for(discard_sleep_duration);
    std::cout << "done\n" << std::flush;
  }
  thread_state_ptr_ = std::make_unique<ThreadState>();
  perf_counter_state_ptr_->pointer = std::make_unique<PerfCounters>();
  const auto test_start_time = jewels::time::SteadyClock::now();
  const auto test_end_time = test_start_time + test_time_;
  auto interval_start_time = test_start_time;
  perf_counter_state_ptr_->pointer->start_time = interval_start_time - test_start_time.time_since_epoch();
  test_thread_ = std::thread([this]() { test_thread_fn(); });
  constexpr size_t lines_per_header = 20U;
  size_t line_count = 0U;
  while (interval_start_time < test_end_time)
  {
    const auto interval_end_time = interval_start_time + std::chrono::seconds(1);
    while (jewels::time::SteadyClock::now() < interval_end_time)
    {
      std::this_thread::sleep_for(interval_end_time - jewels::time::SteadyClock::now());
    }
    auto counters_ptr = std::make_unique<PerfCounters>();
    counters_ptr->start_time = interval_end_time - test_start_time.time_since_epoch();
    {
      const std::lock_guard guard(perf_counter_state_ptr_->mutex);
      perf_counter_state_ptr_->pointer.swap(counters_ptr);
    }
    counters_ptr->end_time = interval_end_time - test_start_time.time_since_epoch();
    if (line_count == 0U)
    {
      PerfCounters::print_header();
    }
    counters_ptr->print();
    line_count = (line_count + 1U) % lines_per_header;
    interval_start_time = interval_end_time;
  }
  thread_state_ptr_->shutdown_flag = true;
  test_thread_.join();
  std::cout << "Deleting test output directory..." << std::flush;
  std::filesystem::remove_all(log_dir_path);
  std::cout << "done\n" << std::flush;
  const std::lock_guard guard(perf_counter_state_ptr_->mutex);
  if (perf_counter_state_ptr_->drop_count != 0)
  {
    std::cerr << "FAILED: drop_count is " << perf_counter_state_ptr_->drop_count << '\n' << std::flush;
    return jewels::unexpected(jewels::MonoError{});
  }
  return {};
}

template <typename MessageHandleType>
void LogPerfImpl<MessageHandleType>::test_thread_fn()
{
  const auto log_path = (std::filesystem::path{output_dir_} / log_dir_name).string();
  if (const auto open_result = writer_.open_log(log_path, log_file_prefix, jewels::time::SteadyClock::now());
      !open_result)
  {
    throw std::runtime_error(
      fmt::format("Failed to open test log at {}: {}", log_path, wise_enum::to_string(open_result.error())));
  }
  if (const auto add_result = writer_.add_channel(
        onboard::LoggedChannelMetadata{
          .channel_name = channel_name,
          .compression_type = CompressionType::none,
          .message_encoding = MessageEncoding::undefined,
          .channel_type = ChannelType::regular,
          .schema_name = schema_name,
          .schema_encoding = SchemaEncoding::undefined,
          .schema_definition = "",
        },
        jewels::time::SteadyClock::now());
      !add_result)
  {
    throw std::runtime_error(fmt::format("Failed to add test channel: {}", wise_enum::to_string(add_result.error())));
  }
  auto now = jewels::time::SteadyClock::now();
  auto last_write_time = now;
  auto last_periodic_callback_time = now;
  while (!thread_state_ptr_->shutdown_flag)
  {
    const auto next_write_time =
      last_write_time + std::chrono::duration_cast<std::chrono::nanoseconds>(write_interval_);
    now = jewels::time::SteadyClock::now();
    while (now < next_write_time)
    {
      std::this_thread::sleep_for(next_write_time - now);
      now = jewels::time::SteadyClock::now();
    }
    if (now - last_periodic_callback_time >= periodic_callback_interval)
    {
      writer_.periodic_callback(now);
      last_periodic_callback_time = now;
    }
    const auto backlog_result = writer_.get_write_backlog(now);
    if (!backlog_result)
    {
      throw std::runtime_error(
        fmt::format("Failed to get write backlog: {}", wise_enum::to_string(backlog_result.error())));
    }
    {
      const std::chrono::duration<double, std::chrono::milliseconds::period> backlog = *backlog_result;
      const std::lock_guard guard(perf_counter_state_ptr_->mutex);
      perf_counter_state_ptr_->max_backlog = std::max(perf_counter_state_ptr_->max_backlog, backlog);
      perf_counter_state_ptr_->pointer->max_backlog = std::max(perf_counter_state_ptr_->pointer->max_backlog, backlog);
    }

    if (const auto inc_result = pinion_buffer_.increment_tail(pinion_buffer_index_ - buffer_layout_.num_slots, 1UL);
        !inc_result)
    {
      throw std::runtime_error("Failed to increment the pinion buffer tail");
    }
    auto pinion_buffer_iterator = std::begin(pinion_buffer_);
    auto pinion_slot = pinion_buffer_iterator.dereference();
    pinion_slot.header()->sequence_number = pinion_buffer_index_;
    pinion_slot.header()->publish_timestamp = now.time_since_epoch().count();
    if (const auto inc_result = pinion_buffer_.increment_head(pinion_buffer_index_, 1UL); !inc_result)
    {
      throw std::runtime_error("Failed to increment the pinion buffer head");
    }
    ++pinion_buffer_index_;

    const auto write_result = writer_.log_clockwork_message(
      channel_name,
      ::clockwork::pinion::SlotRef(jewels::memory::make_non_null_from_ref(pinion_buffer_), pinion_buffer_iterator),
      LogTimestamp{now.time_since_epoch()},
      jewels::time::SteadyClock::now());
    if (!write_result)
    {
      if (write_result.error() != LogError::message_dropped)
      {
        throw std::runtime_error(
          fmt::format("Failed to log test message: {}", wise_enum::to_string(write_result.error())));
      }
      const std::lock_guard guard(perf_counter_state_ptr_->mutex);
      ++perf_counter_state_ptr_->drop_count;
      ++perf_counter_state_ptr_->pointer->drop_count;
    }
    else
    {
      const std::lock_guard guard(perf_counter_state_ptr_->mutex);
      ++perf_counter_state_ptr_->pointer->write_count;
      perf_counter_state_ptr_->pointer->byte_count += message_size_ - zero_size_;
    }
    last_write_time = next_write_time;
  }
  if (const auto close_result = writer_.close_log(jewels::time::SteadyClock::now()); !close_result)
  {
    throw std::runtime_error(fmt::format("Failed to close test log: {}", wise_enum::to_string(close_result.error())));
  }
  if (const auto drain_result = writer_.drain_async_operations(); !drain_result)
  {
    throw std::runtime_error(
      fmt::format("Failed to drain async writes: {}", wise_enum::to_string(drain_result.error())));
  }
}

} // namespace clockwork_logging::tools

int main(int argc, char* argv[]) noexcept
{
  try
  {
    TCLAP::CmdLine cmd("Logging performance tool", ' ', "1.0", true);
    const TCLAP::ValueArg<int64_t> time_arg("t", "test-time", "Test time in seconds", true, 0, "seconds", cmd);
    const TCLAP::ValueArg<uint64_t> rate_arg(
      "r", "data-rate", "Data rate in MiB per second", true, 0U, "MiB per second", cmd);
    const TCLAP::ValueArg<uint64_t> message_size_arg(
      "s", "message-size", "Message size in bytes", true, 0U, "bytes", cmd);
    const TCLAP::ValueArg<uint64_t> zero_size_arg(
      "z", "zero-size", "Number of bytes in the message to zero", false, 0U, "bytes", cmd);
    const TCLAP::ValueArg<std::string> output_dir_arg(
      "o", "output-directory", "Output directory", true, "", "path", cmd);

    try
    {
      cmd.parse(argc, argv);
    }
    catch (const TCLAP::ArgException& exc)
    {
      std::cerr << "Failed to parse command line: " << exc.what() << '\n';
      return 1;
    }

    if (message_size_arg.getValue() == 0U)
    {
      std::cerr << "Invalid message size: " << message_size_arg.getValue() << '\n';
      return 1;
    }
    if (rate_arg.getValue() == 0U)
    {
      std::cerr << "Invalid data rate: " << rate_arg.getValue() << '\n';
      return 1;
    }

    const auto test_time = std::chrono::seconds(time_arg.getValue());
    const auto message_size = message_size_arg.getValue();
    const auto zero_size = zero_size_arg.getValue();
    const auto write_mib_per_sec = rate_arg.getValue();
    const auto& output_dir = output_dir_arg.getValue();

    clockwork_logging::tools::LogPerfImpl<clockwork_logging::onboard::NullMessageHandle> log_perf{
      test_time, message_size, zero_size, write_mib_per_sec, output_dir};
    if (!log_perf.run())
    {
      return 1;
    }

    return 0;
  }
  catch (const std::exception& exc)
  {
    std::cerr << "Caught unexpected exception: " << exc.what() << '\n';
    return 1;
  }
}
