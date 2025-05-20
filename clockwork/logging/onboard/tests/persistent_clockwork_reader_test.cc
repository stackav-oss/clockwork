// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/channel_type.hh"
#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/message_encoding.hh"
#include "clockwork/logging/onboard/async_write_request.hh"
#include "clockwork/logging/onboard/async_writer.hh"
#include "clockwork/logging/onboard/buffered_reader.hh"
#include "clockwork/logging/onboard/clockwork_message_handle.hh"
#include "clockwork/logging/onboard/log_format.hh"
#include "clockwork/logging/onboard/null_message_handle.hh"
#include "clockwork/logging/onboard/reader.hh"
#include "clockwork/logging/onboard/tests/support/test_support.hh"
#include "clockwork/logging/onboard/types.hh"
#include "clockwork/logging/onboard/writer.hh"
#include "clockwork/logging/schema_encoding.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/tests/support/mock_buffer.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/math/constants.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/shared_pool/ref_counted_pool.hh"
#include "jewels/shared_pool/shared_buffer_pool.hh"
#include "jewels/shared_pool/shared_object_pool.hh"
#include "jewels/std/expected.hh"
#include "jewels/std/span.hh"
#include "jewels/testing/filesystem_wrapper.hh"
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/time/sync_time.hh"

#include <catch2/catch_test_macros.hpp>
#include <gsl/util>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <list>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace clockwork_logging::onboard::tests
{
namespace
{

/// Message buffer size
constexpr size_t message_data_size = 12U * jewels::math::constants::bytes_per_kib<size_t>;

struct TestWriterPolicy
{
  /// Data buffer size
  static constexpr size_t buffer_size = 64U * jewels::math::constants::bytes_per_kib<size_t>;

  /// Data buffer alignment
  static constexpr size_t alignment = 512U;

  /// Buffer pool type
  using BufferPoolType = jewels::SharedBufferPool<buffer_size, alignment>;

  /// Maximum number of message handles per write request
  static constexpr size_t max_message_handles = 2U;

  /// Maximum number of buffers per write request
  static constexpr size_t max_buffers = 2U;

  /// I/O ring size
  static constexpr uint32_t io_ring_size = 256U;

  /// Maximum outstanding async operations
  static constexpr size_t max_async_requests = 256U;

  /// Maximum time to hold data in buffers before writing to the log
  static constexpr auto flush_interval = std::chrono::milliseconds(250);

  /// Maximum write size in bytes
  static constexpr size_t max_write_size = 128U * jewels::math::constants::bytes_per_kib<size_t>;

  /// Buffer space to reserve for writing channel and schema metadata to the log in MiB
  static constexpr size_t schema_reserve_mib = 4U;

  /// Buffer space to reserve for writing persistent messages to the log in MiB
  static constexpr size_t persistent_message_reserve_mib = 4U;

  /// Maximum write backlog to accept a new message into the log
  static constexpr auto max_write_backlog = std::chrono::seconds(1);

  /// Maximum log file size
  static constexpr size_t max_log_file_size = 256U * jewels::math::constants::bytes_per_kib<size_t>;

  /// Message handle type
  using MessageHandleType = NullMessageHandle;

  /// Buffer handle type
  using BufferReferenceType = jewels::SharedBufferPool<buffer_size, alignment>::SharedReference;

  /// Async write request handle type
  using AsyncWriteRequestType = AsyncWriteRequest<TestWriterPolicy>;

  /// Async write request handle type
  using AsyncWriteRequestHandleType = jewels::SharedObjectPool<AsyncWriteRequestType>::SharedReference;

  /// Async writer type
  using AsyncWriterType = AsyncWriter<TestWriterPolicy>;
};

struct TestReaderPolicy
{
  /// Filesystem library type
  using FilesystemType = jewels::filesystem::testing::FilesystemWrapper;

  /// Read buffer size
  static constexpr size_t read_buffer_size = 2U * jewels::math::constants::bytes_per_mib<size_t>;

  /// Maximum read size
  static constexpr size_t max_read_size = max_log_record_size;

  /// Minimum size of reads when recovering from I/O error
  static constexpr size_t min_io_error_recover_read_size = 512U;
};

TEST_CASE("Log persistent messages")
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  static constexpr size_t max_write_mib_per_sec = 100U;
  static constexpr auto max_log_file_duration = std::chrono::seconds{0};
  const auto* log_file_prefix = "log_file_";

  constexpr size_t num_slots = 10U;

  constexpr clockwork::pinion::BufferLayout pinion_layout{
    .num_slots = num_slots,
    .message_size = message_data_size,
  };

  clockwork::pinion::support::BufferStorage<pinion_layout> pinion_buffer_storage{};
  const auto pinion_storage_span =
    as_writable_bytes(jewels::as_single_item_span(pinion_buffer_storage))
      .first(sizeof(pinion_buffer_storage) - clockwork::pinion::support::storage_trail_padding<pinion_layout>);
  auto maybe_pinion_buffer = clockwork::pinion::Buffer::try_make(pinion_storage_span, pinion_layout);
  REQUIRE(maybe_pinion_buffer);
  auto pinion_buffer = *maybe_pinion_buffer;

  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto log_dir = test_dir.get_path() / log_file_prefix;

  const auto* schema_name1 = "Schema 1";
  const auto schema_encoding1 = SchemaEncoding::unspecified;
  const auto* schema_definition1 = "Schema definition 1";

  const auto* channel_name1 = "Channel 1";
  const auto compression_type1 = CompressionType::none;
  const auto message_encoding1 = MessageEncoding::unspecified;
  const auto channel_type1 = ChannelType::regular;

  const auto* channel_name2 = "Channel 2";
  const auto compression_type2 = CompressionType::none;
  const auto message_encoding2 = MessageEncoding::unspecified;
  const auto channel_type2 = ChannelType::persistent;

  const jewels::time::SteadyTime time1{std::chrono::seconds(1)};

  Writer<TestWriterPolicy> writer{
    memory_resource, memory_resource, max_write_mib_per_sec, max_log_file_duration, WriterEnvironment::normal};

  REQUIRE(writer.add_channel(
    LoggedChannelMetadata{
      channel_name1,
      compression_type1,
      message_encoding1,
      channel_type1,
      schema_name1,
      schema_encoding1,
      schema_definition1},
    time1));

  REQUIRE(writer.add_channel(
    LoggedChannelMetadata{
      channel_name2,
      compression_type2,
      message_encoding2,
      channel_type2,
      schema_name1,
      schema_encoding1,
      schema_definition1},
    time1));

  constexpr auto message_interval = std::chrono::milliseconds(10);

  constexpr LogTimestamp log_time0{std::chrono::milliseconds(1001)};
  constexpr LogTimestamp message_time0{std::chrono::milliseconds(1000)};
  auto buffer_iterator0 = std::end(pinion_buffer);
  REQUIRE(pinion_buffer.increment_head(0U, 1UL));
  auto slot0 = buffer_iterator0.dereference();
  fill_with_random_bytes(slot0.message());
  slot0.header()->sequence_number = 0U;
  slot0.header()->publish_timestamp = message_time0.get_nanoseconds();
  REQUIRE(writer.save_persistent_clockwork_message(
    channel_name2,
    ClockworkMessageHandle{jewels::memory::make_non_null_from_ref(pinion_buffer), buffer_iterator0},
    log_time0));

  REQUIRE(writer.open_log(log_dir.string(), log_file_prefix, time1));

  constexpr auto log_time1 = log_time0 + message_interval;
  constexpr auto message_time1 = message_time0 + message_interval;
  auto buffer_iterator1 = std::end(pinion_buffer);
  REQUIRE(pinion_buffer.increment_head(1U, 1UL));
  auto slot1 = buffer_iterator1.dereference();
  fill_with_random_bytes(slot1.message());
  slot1.header()->sequence_number = 1U;
  slot1.header()->publish_timestamp = message_time1.get_nanoseconds();

  REQUIRE(writer.log_clockwork_message_wait(
    channel_name1,
    ClockworkMessageHandle{jewels::memory::make_non_null_from_ref(pinion_buffer), buffer_iterator1},
    log_time1,
    time1));

  constexpr auto log_time2 = log_time1 + message_interval;
  constexpr auto message_time2 = message_time1 + message_interval;
  auto buffer_iterator2 = std::end(pinion_buffer);
  REQUIRE(pinion_buffer.increment_head(2U, 1UL));
  auto slot2 = buffer_iterator2.dereference();
  fill_with_random_bytes(slot2.message());
  slot2.header()->sequence_number = 2U;
  slot2.header()->publish_timestamp = message_time2.get_nanoseconds();

  REQUIRE(writer.log_clockwork_message_wait(
    channel_name2,
    ClockworkMessageHandle{jewels::memory::make_non_null_from_ref(pinion_buffer), buffer_iterator2},
    log_time2,
    time1));

  constexpr auto log_time3 = log_time2 + message_interval;
  constexpr auto message_time3 = message_time2 + message_interval;
  auto buffer_iterator3 = std::end(pinion_buffer);
  REQUIRE(pinion_buffer.increment_head(3U, 1UL));
  auto slot3 = buffer_iterator3.dereference();
  fill_with_random_bytes(slot3.message());
  slot3.header()->sequence_number = 3U;
  slot3.header()->publish_timestamp = message_time3.get_nanoseconds();

  REQUIRE(writer.log_clockwork_message_wait(
    channel_name1,
    ClockworkMessageHandle{jewels::memory::make_non_null_from_ref(pinion_buffer), buffer_iterator3},
    log_time3,
    time1));

  REQUIRE(writer.pause_logging(time1));

  constexpr auto log_time4 = log_time3 + message_interval;
  constexpr auto message_time4 = message_time3 + message_interval;
  auto buffer_iterator4 = std::end(pinion_buffer);
  REQUIRE(pinion_buffer.increment_head(4U, 1UL));
  auto slot4 = buffer_iterator4.dereference();
  fill_with_random_bytes(slot4.message());
  slot4.header()->sequence_number = 4U;
  slot4.header()->publish_timestamp = message_time4.get_nanoseconds();

  REQUIRE(writer.save_persistent_clockwork_message(
    channel_name2,
    ClockworkMessageHandle{jewels::memory::make_non_null_from_ref(pinion_buffer), buffer_iterator4},
    log_time4));

  REQUIRE(writer.resume_logging(time1));

  constexpr auto log_time5 = log_time4 + message_interval;
  constexpr auto message_time5 = message_time4 + message_interval;
  auto buffer_iterator5 = std::end(pinion_buffer);
  REQUIRE(pinion_buffer.increment_head(5U, 1UL));
  auto slot5 = buffer_iterator5.dereference();
  fill_with_random_bytes(slot5.message());
  slot5.header()->sequence_number = 5U;
  slot5.header()->publish_timestamp = message_time5.get_nanoseconds();

  REQUIRE(writer.log_clockwork_message_wait(
    channel_name2,
    ClockworkMessageHandle{jewels::memory::make_non_null_from_ref(pinion_buffer), buffer_iterator5},
    log_time5,
    time1));

  constexpr auto log_time6 = log_time5 + message_interval;
  constexpr auto message_time6 = message_time5 + message_interval;
  auto buffer_iterator6 = std::end(pinion_buffer);
  REQUIRE(pinion_buffer.increment_head(6U, 1UL));
  auto slot6 = buffer_iterator6.dereference();
  fill_with_random_bytes(slot6.message());
  slot6.header()->sequence_number = 6U;
  slot6.header()->publish_timestamp = message_time6.get_nanoseconds();

  REQUIRE(writer.log_clockwork_message_wait(
    channel_name1,
    ClockworkMessageHandle{jewels::memory::make_non_null_from_ref(pinion_buffer), buffer_iterator6},
    log_time6,
    time1));

  REQUIRE(writer.pause_logging(time1));

  constexpr auto log_time7 = log_time6 + message_interval;
  constexpr auto message_time7 = message_time6 + message_interval;
  auto buffer_iterator7 = std::end(pinion_buffer);
  REQUIRE(pinion_buffer.increment_head(7U, 1UL));
  auto slot7 = buffer_iterator7.dereference();
  fill_with_random_bytes(slot7.message());
  slot7.header()->sequence_number = 7U;
  slot7.header()->publish_timestamp = message_time7.get_nanoseconds();

  REQUIRE(writer.save_persistent_clockwork_message(
    channel_name2,
    ClockworkMessageHandle{jewels::memory::make_non_null_from_ref(pinion_buffer), buffer_iterator7},
    log_time7));

  REQUIRE(writer.resume_logging(time1));

  constexpr auto log_time8 = log_time7 + message_interval;
  constexpr auto message_time8 = message_time7 + message_interval;
  auto buffer_iterator8 = std::end(pinion_buffer);
  REQUIRE(pinion_buffer.increment_head(8U, 1UL));
  auto slot8 = buffer_iterator8.dereference();
  fill_with_random_bytes(slot8.message());
  slot8.header()->sequence_number = 8U;
  slot8.header()->publish_timestamp = message_time8.get_nanoseconds();

  REQUIRE(writer.log_clockwork_message_wait(
    channel_name1,
    ClockworkMessageHandle{jewels::memory::make_non_null_from_ref(pinion_buffer), buffer_iterator8},
    log_time8,
    time1));

  REQUIRE(writer.close_log(time1));
  REQUIRE(writer.drain_async_operations());

  SECTION("List log files")
  {
    const auto list_result =
      Reader<BufferedReader<TestReaderPolicy>>::list_log_files(memory_resource, log_dir.string());
    REQUIRE(list_result);
    REQUIRE(list_result->size() == 3U);
    REQUIRE(list_result->front() == std::string_view{(log_dir / "log_file_000000.olog").string()});
    REQUIRE(list_result->back() == std::string_view{(log_dir / "log_file_000002.olog").string()});
  }

  SECTION("Get file log interval")
  {
    auto interval_result = Reader<BufferedReader<TestReaderPolicy>>::get_file_log_interval(
      memory_resource, (log_dir / "log_file_000000.olog").string(), TimeFilterOption::log_time);
    REQUIRE(interval_result);
    REQUIRE(interval_result->get_start_timestamp() == log_time1);
    REQUIRE(interval_result->get_end_timestamp() == log_time3);
    interval_result = Reader<BufferedReader<TestReaderPolicy>>::get_file_log_interval(
      memory_resource, (log_dir / "log_file_000000.olog").string(), TimeFilterOption::message_time);
    REQUIRE(interval_result);
    REQUIRE(interval_result->get_start_timestamp() == message_time1);
    REQUIRE(interval_result->get_end_timestamp() == message_time3);
    interval_result = Reader<BufferedReader<TestReaderPolicy>>::get_file_log_interval(
      memory_resource, (log_dir / "log_file_000001.olog").string(), TimeFilterOption::log_time);
    REQUIRE(interval_result);
    REQUIRE(interval_result->get_start_timestamp() == log_time5);
    REQUIRE(interval_result->get_end_timestamp() == log_time6);
    interval_result = Reader<BufferedReader<TestReaderPolicy>>::get_file_log_interval(
      memory_resource, (log_dir / "log_file_000001.olog").string(), TimeFilterOption::message_time);
    REQUIRE(interval_result);
    REQUIRE(interval_result->get_start_timestamp() == message_time5);
    REQUIRE(interval_result->get_end_timestamp() == message_time6);
    interval_result = Reader<BufferedReader<TestReaderPolicy>>::get_file_log_interval(
      memory_resource, (log_dir / "log_file_000002.olog").string(), TimeFilterOption::log_time);
    REQUIRE(interval_result);
    REQUIRE(interval_result->get_start_timestamp() == log_time8);
    REQUIRE(interval_result->get_end_timestamp() == log_time8);
    interval_result = Reader<BufferedReader<TestReaderPolicy>>::get_file_log_interval(
      memory_resource, (log_dir / "log_file_000002.olog").string(), TimeFilterOption::message_time);
    REQUIRE(interval_result);
    REQUIRE(interval_result->get_start_timestamp() == message_time8);
    REQUIRE(interval_result->get_end_timestamp() == message_time8);
    REQUIRE_FALSE(Reader<BufferedReader<TestReaderPolicy>>::get_file_log_interval(
      memory_resource, (log_dir / "log_file_000003.olog").string(), TimeFilterOption::log_time));
  }

  SECTION("Read all messages")
  {
    Reader<BufferedReader<TestReaderPolicy>> reader{memory_resource, log_dir.string(), MetadataMapOption::disable};
    REQUIRE(reader.open());

    auto read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);
    REQUIRE(read_result->sequence_number == 0U);
    REQUIRE(read_result->log_time == log_time0);
    REQUIRE(read_result->message_time == message_time1);
    REQUIRE(read_result->header.empty());
    REQUIRE(std::ranges::equal(read_result->data, slot0.message()));
    REQUIRE(read_result->message_type == LoggedMessageType::repeated_persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name1);
    REQUIRE(read_result->sequence_number == 1U);
    REQUIRE(read_result->log_time == log_time1);
    REQUIRE(read_result->message_time == message_time1);
    REQUIRE(read_result->header.empty());
    REQUIRE(std::ranges::equal(read_result->data, slot1.message()));
    REQUIRE(read_result->message_type == LoggedMessageType::regular);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);
    REQUIRE(read_result->sequence_number == 2U);
    REQUIRE(read_result->log_time == log_time2);
    REQUIRE(read_result->message_time == message_time2);
    REQUIRE(read_result->header.empty());
    REQUIRE(std::ranges::equal(read_result->data, slot2.message()));
    REQUIRE(read_result->message_type == LoggedMessageType::persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name1);
    REQUIRE(read_result->sequence_number == 3U);
    REQUIRE(read_result->log_time == log_time3);
    REQUIRE(read_result->message_time == message_time3);
    REQUIRE(read_result->header.empty());
    REQUIRE(std::ranges::equal(read_result->data, slot3.message()));
    REQUIRE(read_result->message_type == LoggedMessageType::regular);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);
    REQUIRE(read_result->sequence_number == 5U);
    REQUIRE(read_result->log_time == log_time5);
    REQUIRE(read_result->message_time == message_time5);
    REQUIRE(read_result->header.empty());
    REQUIRE(std::ranges::equal(read_result->data, slot5.message()));
    REQUIRE(read_result->message_type == LoggedMessageType::persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name1);
    REQUIRE(read_result->sequence_number == 6U);
    REQUIRE(read_result->log_time == log_time6);
    REQUIRE(read_result->message_time == message_time6);
    REQUIRE(read_result->header.empty());
    REQUIRE(std::ranges::equal(read_result->data, slot6.message()));
    REQUIRE(read_result->message_type == LoggedMessageType::regular);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);
    REQUIRE(read_result->sequence_number == 7U);
    REQUIRE(read_result->log_time == log_time7);
    REQUIRE(read_result->message_time == message_time7);
    REQUIRE(read_result->header.empty());
    REQUIRE(std::ranges::equal(read_result->data, slot7.message()));
    REQUIRE(read_result->message_type == LoggedMessageType::persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name1);
    REQUIRE(read_result->sequence_number == 8U);
    REQUIRE(read_result->log_time == log_time8);
    REQUIRE(read_result->message_time == message_time8);
    REQUIRE(read_result->header.empty());
    REQUIRE(std::ranges::equal(read_result->data, slot8.message()));
    REQUIRE(read_result->message_type == LoggedMessageType::regular);

    REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));
  }

  SECTION("Read with log interval, first message is regular")
  {
    constexpr LogInterval log_interval{log_time3, log_time5};
    const auto list_result =
      Reader<BufferedReader<TestReaderPolicy>>::list_log_files(memory_resource, log_dir.string());
    REQUIRE(list_result);
    Reader<BufferedReader<TestReaderPolicy>> reader{memory_resource, list_result.value(), MetadataMapOption::disable};
    REQUIRE(reader.open(log_interval));

    auto read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);
    REQUIRE(read_result->sequence_number == 2U);
    REQUIRE(read_result->log_time == log_time2);
    REQUIRE(read_result->message_time == message_time3);
    REQUIRE(read_result->header.empty());
    REQUIRE(std::ranges::equal(read_result->data, slot2.message()));
    REQUIRE(read_result->message_type == LoggedMessageType::repeated_persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name1);
    REQUIRE(read_result->sequence_number == 3U);
    REQUIRE(read_result->log_time == log_time3);
    REQUIRE(read_result->message_time == message_time3);
    REQUIRE(read_result->header.empty());
    REQUIRE(std::ranges::equal(read_result->data, slot3.message()));
    REQUIRE(read_result->message_type == LoggedMessageType::regular);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);
    REQUIRE(read_result->sequence_number == 5U);
    REQUIRE(read_result->log_time == log_time5);
    REQUIRE(read_result->message_time == message_time5);
    REQUIRE(read_result->header.empty());
    REQUIRE(std::ranges::equal(read_result->data, slot5.message()));
    REQUIRE(read_result->message_type == LoggedMessageType::persistent);

    REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));
  }

  SECTION("Read with log interval, first message is persistent")
  {
    constexpr LogInterval log_interval{log_time5, log_time7};
    const auto list_result =
      Reader<BufferedReader<TestReaderPolicy>>::list_log_files(memory_resource, log_dir.string());
    REQUIRE(list_result);
    Reader<BufferedReader<TestReaderPolicy>> reader{memory_resource, list_result.value(), MetadataMapOption::disable};
    REQUIRE(reader.open(log_interval));

    auto read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);
    REQUIRE(read_result->sequence_number == 5U);
    REQUIRE(read_result->log_time == log_time5);
    REQUIRE(read_result->message_time == message_time5);
    REQUIRE(read_result->header.empty());
    REQUIRE(std::ranges::equal(read_result->data, slot5.message()));
    REQUIRE(read_result->message_type == LoggedMessageType::persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name1);
    REQUIRE(read_result->sequence_number == 6U);
    REQUIRE(read_result->log_time == log_time6);
    REQUIRE(read_result->message_time == message_time6);
    REQUIRE(read_result->header.empty());
    REQUIRE(std::ranges::equal(read_result->data, slot6.message()));
    REQUIRE(read_result->message_type == LoggedMessageType::regular);

    REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));
  }
}

} // namespace
} // namespace clockwork_logging::onboard::tests
