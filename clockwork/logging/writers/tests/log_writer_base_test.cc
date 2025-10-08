// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/channel_type.hh"
#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/log_writer_config.hh"
#include "clockwork/logging/message_encoding.hh"
#include "clockwork/logging/onboard/buffered_reader.hh"
#include "clockwork/logging/onboard/clockwork_message_handle.hh"
#include "clockwork/logging/onboard/log_format.hh"
#include "clockwork/logging/onboard/reader.hh"
#include "clockwork/logging/onboard/tests/support/test_support.hh"
#include "clockwork/logging/onboard/types.hh"
#include "clockwork/logging/onboard/writer.hh"
#include "clockwork/logging/schema_encoding.hh"
#include "clockwork/logging/writers/channel_message_rates.hh"
#include "clockwork/logging/writers/channel_message_rates_config.hh"
#include "clockwork/logging/writers/log_writer_base.hh"
#include "clockwork/logging/writers/log_writer_state.hh"
#include "clockwork/logging/writers/tests/support/test_log_writer_config.hh"
#include "clockwork/logging/writers/tests/support/test_publisher.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/serialization/py/tests/support/simple_schema_v1.hh"
#include "clockwork/serialization/py/tests/support/simple_schema_v2.hh"
#include "jewels/container/circular_buffer.hh"
#include "jewels/container/compare.hh"
#include "jewels/container/tap/var_array.hh"
#include "jewels/filesystem/filesystem.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/math/constants.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/shared_pool/ref_counted_pool.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <gsl/util>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <memory_resource>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace clockwork_logging::tests
{
namespace
{

/// Pinion unix domain socket namespace
constexpr auto pinion_namespace = "logging_test";

struct TestReaderPolicy
{
  /// Filesystem library type
  using FilesystemType = jewels::filesystem::Filesystem;

  /// Read buffer size
  static constexpr size_t read_buffer_size = 2U * jewels::math::constants::bytes_per_mib<size_t>;

  /// Maximum read size
  static constexpr size_t max_read_size = onboard::max_log_record_size;

  /// Minimum size of reads when recovering from I/O error
  static constexpr size_t min_io_error_recover_read_size = 512U;
};

/// Test writer class
class TestWriter : public LogWriterBase<TestWriter>
{
public:
  /// Construct a TestWriter
  /// @param[in] memory_resource Memory resource
  /// @param[in] pinion_shm_root Pinion shared memory root directory
  /// @param[in] pinion_namespace Pinion unix socket namespace
  /// @param[in] log_writer_config Log writer configuration
  /// @param[in] buffer_pool_size Buffer pool size
  /// @param[in] max_log_file_duration Maximum duration a log file will span
  TestWriter(
    jewels::memory::MemoryResource memory_resource,
    const LogWriterConfigTap& log_writer_config,
    std::string_view pinion_shm_root,
    std::string_view pinion_namespc,
    size_t buffer_pool_size,
    std::chrono::nanoseconds max_log_file_duration,
    bool save_persistent_messages = false);

  using LogWriterBase<TestWriter>::LogWriterBase;
  using LogWriterBase<TestWriter>::log_message;
  using LogWriterBase<TestWriter>::log_message_wait;
  using LogWriterBase<TestWriter>::save_persistent_message;

  /// Received message handler
  /// @param[in] channel_name Channel name
  /// @param[in] message_handle Clockwork message handle
  void message_handler(std::string_view channel_name, const onboard::ClockworkMessageHandle& message_handle);

  /// Set is degraded flag
  /// @param[in] status_string Human readable status string
  void set_is_degraded(std::string_view status_string);

  /// Set state to failed
  /// @param[in] status_string Human readable status string
  void set_state_to_failed(std::string_view status_string);

private:
  bool save_persistent_messages_;
};

TestWriter::TestWriter(
  jewels::memory::MemoryResource memory_resource,
  const LogWriterConfigTap& log_writer_config,
  std::string_view pinion_shm_root,
  std::string_view pinion_namespc,
  size_t buffer_pool_size,
  std::chrono::nanoseconds max_log_file_duration,
  bool save_persistent_messages)
  : LogWriterBase<TestWriter>(
      std::move(memory_resource),
      log_writer_config,
      pinion_shm_root,
      pinion_namespc,
      buffer_pool_size,
      max_log_file_duration,
      {}),
    save_persistent_messages_(save_persistent_messages)
{
}

void TestWriter::message_handler(std::string_view channel_name, const onboard::ClockworkMessageHandle& message_handle)
{
  const auto state = get_state();
  if (state == LogWriterState::logging || state == LogWriterState::degraded)
  {
    const LogTimestamp log_time{jewels::time::SyncClock::now()};
    if (const auto write_result =
          this->log_message(channel_name, message_handle, log_time, jewels::time::SteadyClock::now());
        !write_result)
    {
      jewels::log_cerr_error("Failed to write message on channel {}: {}", channel_name, write_result.error());
    }
  }
  else if (save_persistent_messages_ && is_persistent_channel(channel_name) && state == LogWriterState::stopped)
  {
    if (const auto save_result =
          save_persistent_message(channel_name, message_handle, LogTimestamp{jewels::time::SyncClock::now()});
        !save_result)
    {
      jewels::log_cerr_error("Failed to save persistent message on channel {}: {}", channel_name, save_result.error());
    }
  }
}

void TestWriter::set_is_degraded(std::string_view status_string)
{
  LogWriterBase<TestWriter>::set_is_degraded(status_string);
}

void TestWriter::set_state_to_failed(std::string_view status_string)
{
  LogWriterBase<TestWriter>::set_state_to_failed(status_string);
}

/// Test writer name
constexpr auto writer_name = "test_writer";

/// Number of buffers in the buffer pool
constexpr size_t buffer_pool_size = 50U;

/// Max log file duration. Zero indicates infinite.
constexpr std::chrono::nanoseconds max_log_file_duration{0};

/// Test run interval
constexpr std::chrono::milliseconds run_interval(1);

/// Expected test message
struct ExpectedMessage
{
  /// Channel name
  std::pmr::string channel_name;
  /// Message data
  std::vector<std::byte> data;
  /// Log time
  LogTimestamp log_time;
  /// Transmit time
  LogTimestamp message_time;
  /// Sequence number
  uint32_t sequence_number{};
};

TEST_CASE("Empty log")
{
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto memory_resource = jewels::memory::MemoryResource(std::pmr::new_delete_resource());
  const auto test_log_path = (test_dir.get_path() / writer_name).string();

  const jewels::testing::TmpDirectoryGuard shm_dir;
  const auto writer_config_ptr = get_test_log_writer_config();
  const auto& writer_config = *writer_config_ptr;

  {
    auto test_writer_ptr = std::make_unique<TestWriter>(
      memory_resource,
      writer_config,
      shm_dir.get_path().string(),
      pinion_namespace,
      buffer_pool_size,
      max_log_file_duration);
    auto& test_writer = *test_writer_ptr;

    std::map<std::pmr::string, size_t> expected_message_counts;
    std::vector<TestPublisher> test_publishers;
    test_publishers.reserve(writer_config.get_channels().size());
    for (const auto& channel_config : writer_config.get_channels())
    {
      auto open_result = TestPublisher::try_open(
        memory_resource,
        shm_dir.get_path().string(),
        pinion_namespace,
        channel_config.get_uuid().to_string(),
        channel_config.get_channel_name(),
        channel_config.get_num_slots(),
        channel_config.get_message_size());
      REQUIRE(open_result);
      test_publishers.emplace_back(std::move(open_result).value());
    }

    REQUIRE(test_writer.initialize());
    test_writer.run_for(run_interval);

    for (auto& publisher : test_publishers)
    {
      REQUIRE(publisher.on_connect_pending());
      REQUIRE(publisher.num_clients() == 1U);
      REQUIRE_FALSE(test_writer.get_message_counts().contains(std::pmr::string{publisher.get_channel_name()}));
      expected_message_counts[std::pmr::string(publisher.get_channel_name())] = 0U;
    }

    REQUIRE(test_writer.get_state() == LogWriterState::stopped);
    REQUIRE(test_writer.start_logging_paused(test_log_path));
    REQUIRE(test_writer.get_state() == LogWriterState::paused);
    REQUIRE(test_writer.resume_logging());
    REQUIRE(test_writer.get_state() == LogWriterState::logging);
    REQUIRE(test_writer.pause_logging());
    REQUIRE(test_writer.get_state() == LogWriterState::paused);
    REQUIRE(test_writer.stop_logging());
    REQUIRE(test_writer.get_state() == LogWriterState::stopped);
  }

  // Check the channel metadata
  onboard::Reader<onboard::BufferedReader<TestReaderPolicy>> reader{memory_resource, test_log_path};
  REQUIRE(reader.open());
  REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));

  auto metadata_result = reader.get_channel_metadata("channel1");
  REQUIRE(metadata_result);
  REQUIRE(metadata_result->compression_type == CompressionType::none);
  REQUIRE(metadata_result->message_encoding == MessageEncoding::tachyon);
  REQUIRE(
    metadata_result->schema_name ==
    clockwork::LoggingTraits<clockwork::Tappy<clockwork::tests::SimpleSchemaV1>>::schema_name);
  REQUIRE(metadata_result->schema_encoding == SchemaEncoding::clockwork_tachyon);
  REQUIRE(
    std::ranges::equal(
      metadata_result->schema_definition,
      clockwork::LoggingTraits<clockwork::Tappy<clockwork::tests::SimpleSchemaV1>>::schema_definition));

  metadata_result = reader.get_channel_metadata("channel2");
  REQUIRE(metadata_result);
  REQUIRE(metadata_result->compression_type == CompressionType::none);
  REQUIRE(metadata_result->message_encoding == MessageEncoding::tachyon);
  REQUIRE(
    metadata_result->schema_name ==
    clockwork::LoggingTraits<clockwork::Tappy<clockwork::tests::SimpleSchemaV2>>::schema_name);
  REQUIRE(metadata_result->schema_encoding == SchemaEncoding::clockwork_tachyon);
  REQUIRE(
    std::ranges::equal(
      metadata_result->schema_definition,
      clockwork::LoggingTraits<clockwork::Tappy<clockwork::tests::SimpleSchemaV2>>::schema_definition));
}

TEST_CASE("Log all messages")
{
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto memory_resource = jewels::memory::MemoryResource(std::pmr::new_delete_resource());
  const auto test_log_path = (test_dir.get_path() / writer_name).string();

  const jewels::testing::TmpDirectoryGuard shm_dir;
  const auto writer_config_ptr = get_test_log_writer_config();
  const auto& writer_config = *writer_config_ptr;

  std::vector<ExpectedMessage> expected_messages;

  {
    auto test_writer_ptr = std::make_unique<TestWriter>(
      memory_resource,
      writer_config,
      shm_dir.get_path().string(),
      pinion_namespace,
      buffer_pool_size,
      max_log_file_duration);
    auto& test_writer = *test_writer_ptr;

    std::map<std::pmr::string, size_t> expected_message_counts;
    std::vector<TestPublisher> test_publishers;
    test_publishers.reserve(writer_config.get_channels().size());
    for (const auto& channel_config : writer_config.get_channels())
    {
      auto open_result = TestPublisher::try_open(
        memory_resource,
        shm_dir.get_path().string(),
        pinion_namespace,
        channel_config.get_uuid().to_string(),
        channel_config.get_channel_name(),
        channel_config.get_num_slots(),
        channel_config.get_message_size());
      REQUIRE(open_result);
      test_publishers.emplace_back(std::move(open_result).value());
    }

    REQUIRE(test_writer.initialize());
    test_writer.run_for(run_interval);

    for (auto& publisher : test_publishers)
    {
      REQUIRE(publisher.on_connect_pending());
      REQUIRE(publisher.num_clients() == 1U);
      REQUIRE_FALSE(test_writer.get_message_counts().contains(std::pmr::string{publisher.get_channel_name()}));
      expected_message_counts[std::pmr::string(publisher.get_channel_name())] = 0U;
    }

    REQUIRE(test_writer.start_logging(test_log_path));
    REQUIRE(test_writer.get_state() == LogWriterState::logging);

    constexpr size_t messages_per_channel = 10U;
    constexpr std::chrono::milliseconds message_interval(1);
    LogTimestamp message_time{std::chrono::hours(1)};
    for (uint32_t i = 0U; i < messages_per_channel; ++i)
    {
      for (uint32_t j = 0U; j < writer_config.get_channels().size(); ++j)
      {
        auto& publisher = test_publishers.at(j);
        const auto& channel_config = writer_config.get_channels()[j];
        expected_messages.emplace_back();
        auto& expected_message = expected_messages.back();
        expected_message.channel_name = channel_config.get_channel_name();
        expected_message.data.resize(channel_config.get_message_size());
        onboard::tests::fill_with_random_bytes(expected_message.data);
        expected_message.log_time = LogTimestamp{jewels::time::SyncClock::now().time_since_epoch()};
        expected_message.message_time = message_time;
        expected_message.sequence_number = i;

        REQUIRE(publisher.try_publish(expected_message.message_time, expected_message.data));
        ++expected_message_counts[expected_message.channel_name];
        message_time += message_interval;
      }

      test_writer.run_for(run_interval);
      REQUIRE(test_writer.get_and_reset_max_write_backlog() != std::chrono::nanoseconds(0));
      REQUIRE(test_writer.get_and_reset_max_write_backlog() == std::chrono::nanoseconds(0));
      REQUIRE(test_writer.drain_async_operations());
    }

    const auto& message_counts = test_writer.get_message_counts();
    for (const auto& [expected_channel, expected_count] : expected_message_counts)
    {
      CAPTURE(expected_channel);
      if (expected_count == 0)
      {
        REQUIRE_FALSE(message_counts.contains(expected_channel));
      }
      else
      {
        REQUIRE(message_counts.at(expected_channel) == expected_count);
      }
    }
    test_writer.clear_message_counts();
    REQUIRE(test_writer.get_message_counts().empty());

    REQUIRE(test_writer.stop_logging());
    REQUIRE(test_writer.get_state() == LogWriterState::stopped);
    REQUIRE(test_writer.get_and_reset_drop_count() == 0U);
  }

  // Check the logged messages
  onboard::Reader<onboard::BufferedReader<TestReaderPolicy>> reader{memory_resource, test_log_path};
  REQUIRE(reader.open());
  for (const auto& expected_message : expected_messages)
  {
    const auto read_result = reader.read_next();
    REQUIRE(read_result);
    const auto& logged_message = read_result.value();
    REQUIRE(logged_message.channel_name == expected_message.channel_name);
    REQUIRE(logged_message.sequence_number == expected_message.sequence_number);
    REQUIRE(logged_message.log_time >= expected_message.log_time);
    REQUIRE(logged_message.message_time == expected_message.message_time);
    REQUIRE(logged_message.data.size() == expected_message.data.size());
    REQUIRE(std::memcmp(logged_message.data.data(), expected_message.data.data(), expected_message.data.size()) == 0);
  }
  REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));
}

TEST_CASE("Pause/resume logging")
{
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto memory_resource = jewels::memory::MemoryResource(std::pmr::new_delete_resource());
  const auto test_log_path = (test_dir.get_path() / writer_name).string();

  const jewels::testing::TmpDirectoryGuard shm_dir;
  const auto writer_config_ptr = get_test_log_writer_config();
  const auto& writer_config = *writer_config_ptr;

  std::vector<ExpectedMessage> expected_messages;

  {
    auto test_writer_ptr = std::make_unique<TestWriter>(
      memory_resource,
      writer_config,
      shm_dir.get_path().string(),
      pinion_namespace,
      buffer_pool_size,
      max_log_file_duration);
    auto& test_writer = *test_writer_ptr;

    std::map<std::pmr::string, size_t> expected_message_counts;
    std::vector<TestPublisher> test_publishers;
    test_publishers.reserve(writer_config.get_channels().size());
    for (const auto& channel_config : writer_config.get_channels())
    {
      auto open_result = TestPublisher::try_open(
        memory_resource,
        shm_dir.get_path().string(),
        pinion_namespace,
        channel_config.get_uuid().to_string(),
        channel_config.get_channel_name(),
        channel_config.get_num_slots(),
        channel_config.get_message_size());
      REQUIRE(open_result);
      test_publishers.emplace_back(std::move(open_result).value());
    }

    REQUIRE(test_writer.initialize());
    test_writer.run_for(run_interval);

    for (auto& publisher : test_publishers)
    {
      REQUIRE(publisher.on_connect_pending());
      REQUIRE(publisher.num_clients() == 1U);
      REQUIRE_FALSE(test_writer.get_message_counts().contains(std::pmr::string{publisher.get_channel_name()}));
      expected_message_counts[std::pmr::string(publisher.get_channel_name())] = 0U;
    }

    REQUIRE(test_writer.start_logging(test_log_path));
    REQUIRE(test_writer.get_state() == LogWriterState::logging);

    constexpr size_t messages_per_channel = 10U;
    constexpr std::chrono::milliseconds message_interval(1);
    LogTimestamp message_time{std::chrono::hours(1)};
    for (uint32_t i = 0U; i < messages_per_channel; ++i)
    {
      for (uint32_t j = 0U; j < writer_config.get_channels().size(); ++j)
      {
        auto& publisher = test_publishers.at(j);
        const auto& channel_config = writer_config.get_channels()[j];
        expected_messages.emplace_back();
        auto& expected_message = expected_messages.back();
        expected_message.channel_name = channel_config.get_channel_name();
        expected_message.data.resize(channel_config.get_message_size());
        onboard::tests::fill_with_random_bytes(expected_message.data);
        expected_message.log_time = LogTimestamp{jewels::time::SyncClock::now().time_since_epoch()};
        expected_message.message_time = message_time;
        expected_message.sequence_number = i;

        REQUIRE(publisher.try_publish(expected_message.message_time, expected_message.data));
        ++expected_message_counts[expected_message.channel_name];
        message_time += message_interval;
      }

      test_writer.run_for(run_interval);
      REQUIRE(test_writer.drain_async_operations());
    }

    REQUIRE(test_writer.pause_logging());
    REQUIRE(test_writer.get_state() == LogWriterState::paused);

    for (uint32_t i = messages_per_channel; i < messages_per_channel * 2U; ++i)
    {
      for (uint32_t j = 0U; j < writer_config.get_channels().size(); ++j)
      {
        auto& publisher = test_publishers.at(j);
        const auto& channel_config = writer_config.get_channels()[j];
        std::vector<std::byte> message_data(channel_config.get_message_size());
        onboard::tests::fill_with_random_bytes(message_data);
        REQUIRE(publisher.try_publish(message_time, message_data));
        message_time += message_interval;
      }

      test_writer.run_for(run_interval);
      REQUIRE(test_writer.drain_async_operations());
    }

    REQUIRE(test_writer.resume_logging());
    REQUIRE(test_writer.get_state() == LogWriterState::logging);

    for (uint32_t i = messages_per_channel * 2U; i < messages_per_channel * 3U; ++i)
    {
      for (uint32_t j = 0U; j < writer_config.get_channels().size(); ++j)
      {
        auto& publisher = test_publishers.at(j);
        const auto& channel_config = writer_config.get_channels()[j];
        expected_messages.emplace_back();
        auto& expected_message = expected_messages.back();
        expected_message.channel_name = channel_config.get_channel_name();
        expected_message.data.resize(channel_config.get_message_size());
        onboard::tests::fill_with_random_bytes(expected_message.data);
        expected_message.log_time = LogTimestamp{jewels::time::SyncClock::now().time_since_epoch()};
        expected_message.message_time = message_time;
        expected_message.sequence_number = i;

        REQUIRE(publisher.try_publish(expected_message.message_time, expected_message.data));
        ++expected_message_counts[expected_message.channel_name];
        message_time += message_interval;
      }

      test_writer.run_for(run_interval);
      REQUIRE(test_writer.drain_async_operations());
    }

    const auto& message_counts = test_writer.get_message_counts();
    for (const auto& [expected_channel, expected_count] : expected_message_counts)
    {
      CAPTURE(expected_channel);
      if (expected_count == 0)
      {
        REQUIRE_FALSE(message_counts.contains(expected_channel));
      }
      else
      {
        REQUIRE(message_counts.at(expected_channel) == expected_count);
      }
    }

    REQUIRE(test_writer.stop_logging());
    REQUIRE(test_writer.get_state() == LogWriterState::stopped);
    REQUIRE(test_writer.get_and_reset_drop_count() == 0U);
  }

  // Check the logged messages
  onboard::Reader<onboard::BufferedReader<TestReaderPolicy>> reader{memory_resource, test_log_path};
  REQUIRE(reader.open());
  for (const auto& expected_message : expected_messages)
  {
    const auto read_result = reader.read_next();
    REQUIRE(read_result);
    const auto& logged_message = read_result.value();
    REQUIRE(logged_message.channel_name == expected_message.channel_name);
    REQUIRE(logged_message.sequence_number == expected_message.sequence_number);
    REQUIRE(logged_message.log_time >= expected_message.log_time);
    REQUIRE(logged_message.message_time == expected_message.message_time);
    REQUIRE(logged_message.data.size() == expected_message.data.size());
    REQUIRE(std::memcmp(logged_message.data.data(), expected_message.data.data(), expected_message.data.size()) == 0);
  }
  REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));
}

TEST_CASE("Log onboard messages")
{
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto memory_resource = jewels::memory::MemoryResource(std::pmr::new_delete_resource());
  const auto test_log_path = (test_dir.get_path() / writer_name).string();

  const jewels::testing::TmpDirectoryGuard shm_dir;
  const auto writer_config_ptr = get_test_log_writer_config();
  const auto& writer_config = *writer_config_ptr;

  std::vector<ExpectedMessage> expected_messages;

  {
    auto test_writer_ptr = std::make_unique<TestWriter>(
      memory_resource,
      writer_config,
      shm_dir.get_path().string(),
      pinion_namespace,
      buffer_pool_size,
      max_log_file_duration);
    auto& test_writer = *test_writer_ptr;

    REQUIRE(test_writer.initialize());
    test_writer.run_for(run_interval);

    REQUIRE(test_writer.start_logging(test_log_path));
    REQUIRE(test_writer.get_state() == LogWriterState::logging);

    constexpr size_t messages_per_channel = 10U;
    constexpr std::chrono::milliseconds message_interval(1);
    LogTimestamp log_time{std::chrono::hours(1)};
    LogTimestamp message_time{std::chrono::hours(2)};
    for (uint32_t i = 0U; i < messages_per_channel; ++i)
    {
      for (const auto& channel_config : writer_config.get_channels())
      {
        expected_messages.emplace_back();
        auto& expected_message = expected_messages.back();
        expected_message.channel_name = channel_config.get_channel_name();
        expected_message.data.resize(channel_config.get_message_size());
        onboard::tests::fill_with_random_bytes(expected_message.data);
        expected_message.log_time = log_time;
        expected_message.message_time = message_time;
        expected_message.sequence_number = i;
        const auto data_span = std::as_bytes(std::span{expected_message.data});

        REQUIRE(test_writer_ptr->log_message(
          onboard::ZeroCopyMessage{
            .channel_name = channel_config.get_channel_name(),
            .sequence_number = i,
            .log_time = log_time,
            .message_time = message_time,
            .header = {},
            .data = std::span{&data_span, 1U},
          },
          false,
          jewels::time::SteadyClock::now()));

        log_time += message_interval;
        message_time += message_interval;
      }

      test_writer.run_for(run_interval);
      REQUIRE(test_writer.drain_async_operations());
    }

    REQUIRE(test_writer.pause_logging());
    REQUIRE(test_writer.get_state() == LogWriterState::paused);

    for (const auto& channel_config : writer_config.get_channels())
    {
      if (channel_config.get_channel_type() == ChannelType::persistent)
      {
        expected_messages.emplace_back();
        auto& expected_message = expected_messages.back();
        expected_message.channel_name = channel_config.get_channel_name();
        expected_message.data.resize(channel_config.get_message_size());
        onboard::tests::fill_with_random_bytes(expected_message.data);
        expected_message.log_time = log_time;
        expected_message.message_time = message_time;
        expected_message.sequence_number = messages_per_channel;
        const auto data_span = std::as_bytes(std::span{expected_message.data});

        REQUIRE(test_writer_ptr->save_persistent_message(
          onboard::ZeroCopyMessage{
            .channel_name = channel_config.get_channel_name(),
            .sequence_number = messages_per_channel,
            .log_time = log_time,
            .message_time = message_time,
            .header = {},
            .data = std::span{&data_span, 1U},
          },
          false));

        log_time += message_interval;
        message_time += message_interval;
        break;
      }
    }

    REQUIRE(test_writer.resume_logging());
    REQUIRE(test_writer.get_state() == LogWriterState::logging);

    for (uint32_t i = messages_per_channel + 1U; i < (messages_per_channel * 2U) + 1U; ++i)
    {
      for (const auto& channel_config : writer_config.get_channels())
      {
        expected_messages.emplace_back();
        auto& expected_message = expected_messages.back();
        expected_message.channel_name = channel_config.get_channel_name();
        expected_message.data.resize(channel_config.get_message_size());
        onboard::tests::fill_with_random_bytes(expected_message.data);
        expected_message.log_time = log_time;
        expected_message.message_time = message_time;
        expected_message.sequence_number = i;
        const auto data_span = std::as_bytes(std::span{expected_message.data});

        REQUIRE(test_writer_ptr->log_message_wait(
          onboard::ZeroCopyMessage{
            .channel_name = channel_config.get_channel_name(),
            .sequence_number = i,
            .log_time = log_time,
            .message_time = message_time,
            .header = {},
            .data = std::span{&data_span, 1U},
          },
          false,
          jewels::time::SteadyClock::now()));

        log_time += message_interval;
        message_time += message_interval;
      }

      test_writer.run_for(run_interval);
      REQUIRE(test_writer.drain_async_operations());
    }

    REQUIRE(test_writer.stop_logging());
    REQUIRE(test_writer.get_state() == LogWriterState::stopped);
    REQUIRE(test_writer.get_and_reset_drop_count() == 0U);
  }

  // Check the logged messages
  onboard::Reader<onboard::BufferedReader<TestReaderPolicy>> reader{memory_resource, test_log_path};
  REQUIRE(reader.open());
  for (const auto& expected_message : expected_messages)
  {
    const auto read_result = reader.read_next();
    REQUIRE(read_result);
    const auto& logged_message = read_result.value();
    REQUIRE(logged_message.channel_name == expected_message.channel_name);
    REQUIRE(logged_message.sequence_number == expected_message.sequence_number);
    REQUIRE(logged_message.log_time == expected_message.log_time);
    CHECK(logged_message.message_time == expected_message.message_time);
    REQUIRE(logged_message.data.size() == expected_message.data.size());
    REQUIRE(std::memcmp(logged_message.data.data(), expected_message.data.data(), expected_message.data.size()) == 0);
  }
  REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));
}

TEST_CASE("Writer only logs first message from buffer after subscribing when channel is persistent")
{
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto memory_resource = jewels::memory::MemoryResource(std::pmr::new_delete_resource());
  const auto test_log_path = (test_dir.get_path() / writer_name).string();

  const jewels::testing::TmpDirectoryGuard shm_dir;
  const auto writer_config_ptr = get_test_log_writer_config();
  const auto& writer_config = *writer_config_ptr;

  std::vector<ExpectedMessage> expected_messages;
  ExpectedMessage expected_persistent_message;

  {
    std::vector<TestPublisher> test_publishers;
    test_publishers.reserve(writer_config.get_channels().size());
    for (const auto& channel_config : writer_config.get_channels())
    {
      auto open_result = TestPublisher::try_open(
        memory_resource,
        shm_dir.get_path().string(),
        pinion_namespace,
        channel_config.get_uuid().to_string(),
        channel_config.get_channel_name(),
        channel_config.get_num_slots(),
        channel_config.get_message_size());
      REQUIRE(open_result);
      test_publishers.emplace_back(std::move(open_result).value());
    }

    for (auto& publisher : test_publishers)
    {
      REQUIRE(publisher.on_connect_pending());
      REQUIRE(publisher.num_clients() == 0U);
    }

    std::map<std::pmr::string, size_t> expected_message_counts;

    constexpr size_t messages_before_subscribe = 5U;
    constexpr std::chrono::milliseconds message_interval(1);
    LogTimestamp message_time{std::chrono::hours(1)};
    for (uint32_t i = 0U; i < messages_before_subscribe; ++i)
    {
      for (uint32_t j = 0U; j < writer_config.get_channels().size(); ++j)
      {
        auto& publisher = test_publishers.at(j);
        const auto& channel_config = writer_config.get_channels()[j];
        ExpectedMessage expected_message{};
        expected_message.channel_name = channel_config.get_channel_name();
        expected_message.data.resize(channel_config.get_message_size());
        onboard::tests::fill_with_random_bytes(expected_message.data);
        expected_message.log_time = LogTimestamp{jewels::time::SyncClock::now().time_since_epoch()};
        expected_message.message_time = message_time;
        expected_message.sequence_number = i;
        REQUIRE(publisher.try_publish(expected_message.message_time, expected_message.data));

        if ((i == messages_before_subscribe - 1U) && (channel_config.get_channel_type() == ChannelType::persistent))
        {
          expected_persistent_message = std::move(expected_message);
        }
        message_time += message_interval;
      }
    }

    expected_persistent_message.message_time = message_time;

    for (auto& publisher : test_publishers)
    {
      REQUIRE(publisher.on_connect_pending());
      REQUIRE(publisher.num_clients() == 0U);
    }

    auto test_writer_ptr = std::make_unique<TestWriter>(
      memory_resource,
      writer_config,
      shm_dir.get_path().string(),
      pinion_namespace,
      buffer_pool_size,
      max_log_file_duration,
      /*save_persistent_messages=*/true);
    auto& test_writer = *test_writer_ptr;
    REQUIRE(test_writer.initialize());
    test_writer.run_for(run_interval);
    REQUIRE(test_writer.start_logging(test_log_path));

    for (auto& publisher : test_publishers)
    {
      REQUIRE(publisher.on_connect_pending());
      REQUIRE(publisher.num_clients() == 1U);
    }

    test_writer.run_for(run_interval);

    {
      const auto& message_counts = test_writer.get_message_counts();
      for (const auto& [expected_channel, expected_count] : expected_message_counts)
      {
        CAPTURE(expected_channel);
        REQUIRE(message_counts.at(expected_channel) == expected_count);
      }
    }

    constexpr size_t messages_per_channel = 10U;
    for (uint32_t i = messages_before_subscribe; i < messages_before_subscribe + messages_per_channel; ++i)
    {
      for (uint32_t j = 0U; j < writer_config.get_channels().size(); ++j)
      {
        auto& publisher = test_publishers.at(j);
        const auto& channel_config = writer_config.get_channels()[j];
        expected_messages.emplace_back();
        auto& expected_message = expected_messages.back();
        expected_message.channel_name = channel_config.get_channel_name();
        expected_message.data.resize(channel_config.get_message_size());
        onboard::tests::fill_with_random_bytes(expected_message.data);
        expected_message.log_time = LogTimestamp{jewels::time::SyncClock::now().time_since_epoch()};
        expected_message.message_time = message_time;
        expected_message.sequence_number = i;

        REQUIRE(publisher.try_publish(expected_message.message_time, expected_message.data));
        ++expected_message_counts[expected_message.channel_name];
        message_time += message_interval;
      }

      test_writer.run_for(run_interval);
      REQUIRE(test_writer.drain_async_operations());
      const auto& message_counts = test_writer.get_message_counts();
      for (const auto& [expected_channel, expected_count] : expected_message_counts)
      {
        CAPTURE(expected_channel);
        REQUIRE(message_counts.at(expected_channel) == expected_count);
      }
    }

    REQUIRE(test_writer.drain_async_operations());
    REQUIRE(test_writer.get_and_reset_drop_count() == 0U);
  }

  // Check the logged messages
  onboard::Reader<onboard::BufferedReader<TestReaderPolicy>> reader{memory_resource, test_log_path};
  REQUIRE(reader.open());
  {
    const auto read_result = reader.read_next();
    REQUIRE(read_result);
    const auto& logged_message = read_result.value();
    REQUIRE(logged_message.channel_name == expected_persistent_message.channel_name);
    REQUIRE(logged_message.sequence_number == expected_persistent_message.sequence_number);
    REQUIRE(logged_message.log_time >= expected_persistent_message.log_time);
    REQUIRE(logged_message.message_time == expected_persistent_message.message_time);
    REQUIRE(logged_message.data.size() == expected_persistent_message.data.size());
    REQUIRE(
      std::memcmp(
        logged_message.data.data(), expected_persistent_message.data.data(), expected_persistent_message.data.size()) ==
      0);
  }
  for (const auto& expected_message : expected_messages)
  {
    const auto read_result = reader.read_next();
    REQUIRE(read_result);
    const auto& logged_message = read_result.value();
    REQUIRE(logged_message.channel_name == expected_message.channel_name);
    REQUIRE(logged_message.sequence_number == expected_message.sequence_number);
    REQUIRE(logged_message.log_time >= expected_message.log_time);
    REQUIRE(logged_message.message_time == expected_message.message_time);
    REQUIRE(logged_message.data.size() == expected_message.data.size());
    REQUIRE(std::memcmp(logged_message.data.data(), expected_message.data.data(), expected_message.data.size()) == 0);
  }
  REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));
}

TEST_CASE("Detect drops on buffer overrun")
{
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto memory_resource = jewels::memory::MemoryResource(std::pmr::new_delete_resource());
  const auto test_log_path = (test_dir.get_path() / writer_name).string();

  const jewels::testing::TmpDirectoryGuard shm_dir;
  const auto writer_config_ptr = get_test_log_writer_config();
  const auto& writer_config = *writer_config_ptr;

  {
    auto test_writer_ptr = std::make_unique<TestWriter>(
      memory_resource,
      writer_config,
      shm_dir.get_path().string(),
      pinion_namespace,
      buffer_pool_size,
      max_log_file_duration);
    auto& test_writer = *test_writer_ptr;

    std::map<std::pmr::string, size_t> expected_message_counts;
    std::vector<TestPublisher> test_publishers;
    test_publishers.reserve(writer_config.get_channels().size());
    for (const auto& channel_config : writer_config.get_channels())
    {
      auto open_result = TestPublisher::try_open(
        memory_resource,
        shm_dir.get_path().string(),
        pinion_namespace,
        channel_config.get_uuid().to_string(),
        channel_config.get_channel_name(),
        channel_config.get_num_slots(),
        channel_config.get_message_size());
      REQUIRE(open_result);
      test_publishers.emplace_back(std::move(open_result).value());
    }

    REQUIRE(test_writer.initialize());
    test_writer.run_for(run_interval);
    REQUIRE(test_writer.start_logging(test_log_path));

    for (auto& publisher : test_publishers)
    {
      REQUIRE(publisher.on_connect_pending());
      REQUIRE(publisher.num_clients() == 1U);
      REQUIRE_FALSE(test_writer.get_message_counts().contains(std::pmr::string{publisher.get_channel_name()}));
      expected_message_counts[std::pmr::string(publisher.get_channel_name())] = 0U;
    }

    constexpr size_t messages_per_channel = 10U;
    constexpr std::chrono::milliseconds message_interval(1);
    LogTimestamp message_time{std::chrono::hours(1)};

    /// These messages will be dropped
    for (uint32_t i = 0U; i < writer_config.get_channels().size(); ++i)
    {
      auto& publisher = test_publishers.at(i);
      const auto& channel_config = writer_config.get_channels()[i];
      REQUIRE(channel_config.get_num_slots() == static_cast<int32_t>(messages_per_channel));
      ExpectedMessage expected_message{};
      expected_message.channel_name = channel_config.get_channel_name();
      expected_message.data.resize(channel_config.get_message_size());
      onboard::tests::fill_with_random_bytes(expected_message.data);
      expected_message.log_time = LogTimestamp{jewels::time::SyncClock::now().time_since_epoch()};
      expected_message.message_time = message_time;
      expected_message.sequence_number = 0U;

      REQUIRE(publisher.try_publish(expected_message.message_time, expected_message.data));
      message_time += message_interval;
    }

    for (uint32_t i = 0U; i < writer_config.get_channels().size(); ++i)
    {
      for (uint32_t j = 1U; j <= messages_per_channel; ++j)
      {
        auto& publisher = test_publishers.at(i);
        const auto& channel_config = writer_config.get_channels()[i];
        ExpectedMessage expected_message{};
        expected_message.channel_name = channel_config.get_channel_name();
        expected_message.data.resize(channel_config.get_message_size());
        onboard::tests::fill_with_random_bytes(expected_message.data);
        expected_message.log_time = LogTimestamp{jewels::time::SyncClock::now().time_since_epoch()};
        expected_message.message_time = message_time;
        expected_message.sequence_number = j;

        REQUIRE(publisher.try_publish(expected_message.message_time, expected_message.data));
        ++expected_message_counts[expected_message.channel_name];
        message_time += message_interval;
      }
    }

    test_writer.run_for(run_interval);

    REQUIRE(test_writer.drain_async_operations());
    REQUIRE(test_writer.get_and_reset_drop_count() == 3U);

    const auto& message_counts = test_writer.get_message_counts();
    for (const auto& [expected_channel, expected_count] : expected_message_counts)
    {
      REQUIRE(message_counts.at(expected_channel) == expected_count);
    }
  }

  // Read the log and check that sequence number 0 was dropped for each channel
  onboard::Reader<onboard::BufferedReader<TestReaderPolicy>> reader{memory_resource, test_log_path};
  REQUIRE(reader.open());
  constexpr size_t expected_message_count = 30U;
  for (size_t i = 0U; i < expected_message_count; ++i)
  {
    const auto read_result = reader.read_next();
    REQUIRE(read_result);
    const auto& logged_message = read_result.value();
    CHECK(logged_message.sequence_number != 0U);
  }
  REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));
}

TEST_CASE("Set state to failed")
{
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto memory_resource = jewels::memory::MemoryResource(std::pmr::new_delete_resource());
  const auto test_log_path = (test_dir.get_path() / writer_name).string();

  const jewels::testing::TmpDirectoryGuard shm_dir;
  const auto writer_config_ptr = get_test_log_writer_config();
  const auto& writer_config = *writer_config_ptr;

  std::vector<TestPublisher> test_publishers;
  test_publishers.reserve(writer_config.get_channels().size());
  for (const auto& channel_config : writer_config.get_channels())
  {
    auto open_result = TestPublisher::try_open(
      memory_resource,
      shm_dir.get_path().string(),
      pinion_namespace,
      channel_config.get_uuid().to_string(),
      channel_config.get_channel_name(),
      channel_config.get_num_slots(),
      channel_config.get_message_size());
    REQUIRE(open_result);
    test_publishers.emplace_back(std::move(open_result).value());
  }

  auto test_writer_ptr = std::make_unique<TestWriter>(
    memory_resource,
    writer_config,
    shm_dir.get_path().string(),
    pinion_namespace,
    buffer_pool_size,
    max_log_file_duration);
  auto& test_writer = *test_writer_ptr;
  REQUIRE(test_writer.initialize());
  test_writer.run_for(run_interval);

  for (auto& publisher : test_publishers)
  {
    REQUIRE(publisher.on_connect_pending());
    REQUIRE(publisher.num_clients() == 1U);
  }

  REQUIRE(test_writer.start_logging(test_log_path));
  REQUIRE(test_writer.get_state() == LogWriterState::logging);
  REQUIRE(test_writer.get_status().status_string.empty());
  test_writer.set_state_to_failed("Setting state to failed");
  REQUIRE(test_writer.get_state() == LogWriterState::failed);
  REQUIRE(test_writer.get_status().status_string == "Setting state to failed");
  REQUIRE(test_writer.stop_logging() == jewels::unexpected(LogError::failed));
}

TEST_CASE("Set is degraded")
{
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto memory_resource = jewels::memory::MemoryResource(std::pmr::new_delete_resource());
  const auto test_log_path = (test_dir.get_path() / writer_name).string();

  const jewels::testing::TmpDirectoryGuard shm_dir;
  const auto writer_config_ptr = get_test_log_writer_config();
  const auto& writer_config = *writer_config_ptr;

  std::vector<TestPublisher> test_publishers;
  test_publishers.reserve(writer_config.get_channels().size());
  for (const auto& channel_config : writer_config.get_channels())
  {
    auto open_result = TestPublisher::try_open(
      memory_resource,
      shm_dir.get_path().string(),
      pinion_namespace,
      channel_config.get_uuid().to_string(),
      channel_config.get_channel_name(),
      channel_config.get_num_slots(),
      channel_config.get_message_size());
    REQUIRE(open_result);
    test_publishers.emplace_back(std::move(open_result).value());
  }

  auto test_writer_ptr = std::make_unique<TestWriter>(
    memory_resource,
    writer_config,
    shm_dir.get_path().string(),
    pinion_namespace,
    buffer_pool_size,
    max_log_file_duration);
  auto& test_writer = *test_writer_ptr;
  REQUIRE(test_writer.initialize());
  test_writer.run_for(run_interval);

  for (auto& publisher : test_publishers)
  {
    REQUIRE(publisher.on_connect_pending());
    REQUIRE(publisher.num_clients() == 1U);
  }

  REQUIRE(test_writer.start_logging(test_log_path));
  REQUIRE(test_writer.get_state() == LogWriterState::logging);
  REQUIRE(test_writer.get_status().status_string.empty());
  test_writer.set_is_degraded("Setting is degraded");
  REQUIRE(test_writer.get_state() == LogWriterState::degraded);
  REQUIRE(test_writer.get_status().status_string == "Setting is degraded");
  REQUIRE(test_writer.stop_logging());
}

TEST_CASE("Initialization throws if number of channels exceeds limit")
{
  const auto memory_resource = jewels::memory::MemoryResource(std::pmr::new_delete_resource());

  const jewels::testing::TmpDirectoryGuard shm_dir;

  auto log_writer_config_ptr = std::make_unique<LogWriterConfigTap>();

  if (LogWriterConfigTap::max_num_channels <= ChannelMessageRatesTap::max_num_channels)
  {
    // Nothing to test in this case. The log writer config will never contain more channels than there are in channel
    // message rates.
    return;
  }
  // Set up the log writer config with too many channels
  for (uint32_t i = 0; i < ChannelMessageRatesTap::max_num_channels + 1; ++i)
  {
    log_writer_config_ptr->get_underlying_channels().emplace_back();
  }

  auto test_writer_ptr = std::make_unique<TestWriter>(
    memory_resource,
    *log_writer_config_ptr,
    shm_dir.get_path().string(),
    pinion_namespace,
    buffer_pool_size,
    max_log_file_duration);
  REQUIRE_THROWS_AS(test_writer_ptr->initialize(), std::invalid_argument);
}
} // namespace
} // namespace clockwork_logging::tests
