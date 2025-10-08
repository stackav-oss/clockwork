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
#include "jewels/aligner/aligner.hh"
#include "jewels/container/circular_buffer.hh"
#include "jewels/filesystem/filesystem.hh"
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

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <gsl/util>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iterator>
#include <list>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

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

TEST_CASE("Read metadata")
{
  const auto metadata_map_option = GENERATE(MetadataMapOption::enable, MetadataMapOption::disable);
  CAPTURE(metadata_map_option);

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  static constexpr size_t max_write_mib_per_sec = 100U;
  static constexpr auto max_log_file_duration = std::chrono::seconds{0};
  const auto* log_file_prefix = "log_file_";

  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto log_dir = test_dir.get_path() / log_file_prefix;

  const auto* schema_name1 = "Schema 1";
  const auto schema_encoding1 = SchemaEncoding::unspecified;
  const auto* schema_definition1 = "Schema definition 1";

  const auto* schema_name2 = "Schema 2";
  const auto schema_encoding2 = SchemaEncoding::undefined;
  const auto* schema_definition2 = "";

  const auto* channel_name1 = "Channel 1";
  const auto compression_type1 = CompressionType::none;
  const auto message_encoding1 = MessageEncoding::unspecified;
  const auto channel_type1 = ChannelType::persistent;

  const auto* channel_name2 = "Channel 2";
  const auto compression_type2 = CompressionType::none;
  const auto message_encoding2 = MessageEncoding::unspecified;
  const auto channel_type2 = ChannelType::regular;

  const auto* channel_name3 = "Channel 3";
  const auto compression_type3 = CompressionType::none;
  const auto message_encoding3 = MessageEncoding::unspecified;
  const auto channel_type3 = ChannelType::persistent;

  const auto* channel_name4 = "Channel 4";
  const auto compression_type4 = CompressionType::none;
  const auto message_encoding4 = MessageEncoding::unspecified;
  const auto channel_type4 = ChannelType::regular;

  const jewels::time::SteadyTime time1{std::chrono::seconds(1)};

  constexpr size_t num_slots = 2U;

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

  Writer<TestWriterPolicy> writer{
    memory_resource, memory_resource, max_write_mib_per_sec, max_log_file_duration, WriterEnvironment::normal};
  REQUIRE(writer.open_log(log_dir.string(), log_file_prefix, time1));

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

  const auto channel2_offset_result = writer.get_log_file_offset();
  REQUIRE(channel2_offset_result);
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

  constexpr auto log_time1 = LogTimestamp(std::chrono::nanoseconds(10));
  constexpr auto message_time1 = LogTimestamp(std::chrono::nanoseconds(100));

  auto buffer_iterator = std::end(pinion_buffer);
  REQUIRE(pinion_buffer.increment_head(0U, 1UL));

  auto slot = buffer_iterator.dereference();
  fill_with_random_bytes(slot.message());
  slot.header()->sequence_number = 1U;
  slot.header()->publish_timestamp = message_time1.get_nanoseconds();

  REQUIRE(writer.log_clockwork_message_wait(
    channel_name2,
    ClockworkMessageHandle{jewels::memory::make_non_null_from_ref(pinion_buffer), buffer_iterator},
    log_time1,
    time1));

  REQUIRE(writer.add_channel(
    LoggedChannelMetadata{
      channel_name3,
      compression_type3,
      message_encoding3,
      channel_type3,
      schema_name2,
      schema_encoding2,
      schema_definition2},
    time1));

  REQUIRE(writer.add_channel(
    LoggedChannelMetadata{
      channel_name4,
      compression_type4,
      message_encoding4,
      channel_type4,
      schema_name2,
      schema_encoding2,
      schema_definition2},
    time1));

  writer.periodic_callback(time1);
  REQUIRE(writer.close_log(time1));
  REQUIRE(writer.drain_async_operations());

  SECTION("Get logged metadata")
  {
    Reader<BufferedReader<TestReaderPolicy>> reader{memory_resource, log_dir.string(), metadata_map_option};
    REQUIRE_FALSE(reader);
    REQUIRE(reader.open());

    const auto read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);
    REQUIRE(read_result->sequence_number == 1U);
    REQUIRE(read_result->log_time == log_time1);
    REQUIRE(read_result->message_time == message_time1);
    REQUIRE(read_result->header.empty());
    REQUIRE(read_result->data.size() == slot.message().size());
    REQUIRE(std::ranges::equal(read_result->data, slot.message()));
    REQUIRE(read_result->message_type == LoggedMessageType::regular);

    REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));

    auto metadata_result = reader.get_channel_metadata(channel_name1);
    REQUIRE(metadata_result);
    REQUIRE(metadata_result->compression_type == compression_type1);
    REQUIRE(metadata_result->message_encoding == message_encoding1);
    REQUIRE(metadata_result->channel_type == channel_type1);
    REQUIRE(metadata_result->schema_name == schema_name1);
    REQUIRE(metadata_result->schema_encoding == schema_encoding1);
    REQUIRE(metadata_result->schema_definition == schema_definition1);

    metadata_result = reader.get_channel_metadata(channel_name2);
    REQUIRE(metadata_result);
    REQUIRE(metadata_result->compression_type == compression_type2);
    REQUIRE(metadata_result->message_encoding == message_encoding2);
    REQUIRE(metadata_result->channel_type == channel_type2);
    REQUIRE(metadata_result->schema_name == schema_name1);
    REQUIRE(metadata_result->schema_encoding == schema_encoding1);
    REQUIRE(metadata_result->schema_definition == schema_definition1);

    metadata_result = reader.get_channel_metadata(channel_name3);
    REQUIRE(metadata_result);
    REQUIRE(metadata_result->compression_type == compression_type3);
    REQUIRE(metadata_result->message_encoding == message_encoding3);
    REQUIRE(metadata_result->channel_type == channel_type3);
    REQUIRE(metadata_result->schema_name == schema_name2);
    REQUIRE(metadata_result->schema_encoding == schema_encoding2);
    REQUIRE(metadata_result->schema_definition == schema_definition2);

    metadata_result = reader.get_channel_metadata(channel_name4);
    REQUIRE(metadata_result);
    REQUIRE(metadata_result->compression_type == compression_type4);
    REQUIRE(metadata_result->message_encoding == message_encoding4);
    REQUIRE(metadata_result->channel_type == channel_type4);
    REQUIRE(metadata_result->schema_name == schema_name2);
    REQUIRE(metadata_result->schema_encoding == schema_encoding2);
    REQUIRE(metadata_result->schema_definition == schema_definition2);

    const auto& counters = reader.get_error_counters();
    REQUIRE(counters.open_failures == 0U);
    REQUIRE(counters.invalid_log_headers == 0U);
    REQUIRE(counters.advance_errors == 0U);
    REQUIRE(counters.invalid_records == 0U);
    REQUIRE(counters.missing_schema_metadata == 0U);
    REQUIRE(counters.missing_channel_metadata == 0U);

    reader.close();
    REQUIRE_FALSE(reader);
  }

  SECTION("Get logged metadata map")
  {
    Reader<BufferedReader<TestReaderPolicy>> reader{memory_resource, log_dir.string(), metadata_map_option};
    REQUIRE_FALSE(reader);
    REQUIRE(reader.open());

    if (metadata_map_option == MetadataMapOption::disable)
    {
      REQUIRE(reader.get_channel_metadata_map() == jewels::unexpected(LogError::metadata_map_disabled));
    }
    else
    {
      const auto map_result = reader.get_channel_metadata_map();
      REQUIRE(map_result);
      const auto& metadata_map = map_result.value();

      REQUIRE(metadata_map.contains(channel_name1));
      auto metadata = metadata_map.at(channel_name1);
      REQUIRE(metadata.compression_type == compression_type1);
      REQUIRE(metadata.message_encoding == message_encoding1);
      REQUIRE(metadata.channel_type == channel_type1);
      REQUIRE(metadata.schema_name == schema_name1);
      REQUIRE(metadata.schema_encoding == schema_encoding1);
      REQUIRE(metadata.schema_definition == schema_definition1);

      REQUIRE(metadata_map.contains(channel_name2));
      metadata = metadata_map.at(channel_name2);
      REQUIRE(metadata.compression_type == compression_type2);
      REQUIRE(metadata.message_encoding == message_encoding2);
      REQUIRE(metadata.channel_type == channel_type2);
      REQUIRE(metadata.schema_name == schema_name1);
      REQUIRE(metadata.schema_encoding == schema_encoding1);
      REQUIRE(metadata.schema_definition == schema_definition1);

      REQUIRE(metadata_map.contains(channel_name3));
      metadata = metadata_map.at(channel_name3);
      REQUIRE(metadata.compression_type == compression_type3);
      REQUIRE(metadata.message_encoding == message_encoding3);
      REQUIRE(metadata.channel_type == channel_type3);
      REQUIRE(metadata.schema_name == schema_name2);
      REQUIRE(metadata.schema_encoding == schema_encoding2);
      REQUIRE(metadata.schema_definition == schema_definition2);

      REQUIRE(metadata_map.contains(channel_name4));
      metadata = metadata_map.at(channel_name4);
      REQUIRE(metadata.compression_type == compression_type4);
      REQUIRE(metadata.message_encoding == message_encoding4);
      REQUIRE(metadata.channel_type == channel_type4);
      REQUIRE(metadata.schema_name == schema_name2);
      REQUIRE(metadata.schema_encoding == schema_encoding2);
      REQUIRE(metadata.schema_definition == schema_definition2);

      reader.close();
      REQUIRE_FALSE(reader);
    }
  }

  SECTION("Corrupted log header")
  {
    REQUIRE(corrupt_log_file((log_dir / "log_file_000000.olog").string(), 0U, "X"));
    Reader<BufferedReader<TestReaderPolicy>> reader{memory_resource, log_dir.string(), metadata_map_option};
    REQUIRE(reader.open());

    const auto read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);

    REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));

    REQUIRE(reader.get_channel_metadata(channel_name1));
    REQUIRE(reader.get_channel_metadata(channel_name2));
    REQUIRE(reader.get_channel_metadata(channel_name3));
    REQUIRE(reader.get_channel_metadata(channel_name4));

    const auto& counters = reader.get_error_counters();
    REQUIRE(counters.open_failures == 0U);
    REQUIRE(counters.invalid_log_headers == 2U);
    REQUIRE(counters.advance_errors == 0U);
    REQUIRE(counters.invalid_records == 2U);
    REQUIRE(counters.missing_schema_metadata == 0U);
    REQUIRE(counters.missing_channel_metadata == 0U);
  }

  SECTION("Corrupted schema record")
  {
    REQUIRE(
      corrupt_log_file((log_dir / "log_file_000000.olog").string(), log_header_size + schema_record_header_size, "X"));
    Reader<BufferedReader<TestReaderPolicy>> reader{memory_resource, log_dir.string(), metadata_map_option};
    REQUIRE(reader.open());

    const auto read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);

    REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));

    const auto metadata_result1 = reader.get_channel_metadata(channel_name1);
    REQUIRE(metadata_result1);
    REQUIRE(metadata_result1->compression_type == compression_type1);
    REQUIRE(metadata_result1->message_encoding == message_encoding1);
    REQUIRE(metadata_result1->channel_type == channel_type1);
    const std::string_view missing_schema_prefix = "missing_schema_";
    REQUIRE(metadata_result1->schema_name.substr(0U, missing_schema_prefix.size()) == missing_schema_prefix);
    REQUIRE(metadata_result1->schema_encoding == SchemaEncoding::undefined);
    REQUIRE(metadata_result1->schema_definition.empty());

    const auto metadata_result2 = reader.get_channel_metadata(channel_name2);
    REQUIRE(metadata_result2);
    REQUIRE(metadata_result2->compression_type == compression_type2);
    REQUIRE(metadata_result2->message_encoding == message_encoding2);
    REQUIRE(metadata_result2->channel_type == channel_type2);
    REQUIRE(metadata_result2->schema_name == metadata_result1->schema_name);
    REQUIRE(metadata_result2->schema_encoding == SchemaEncoding::undefined);
    REQUIRE(metadata_result2->schema_definition.empty());

    REQUIRE(reader.get_channel_metadata(channel_name3));
    REQUIRE(reader.get_channel_metadata(channel_name4));

    const auto& counters = reader.get_error_counters();
    REQUIRE(counters.open_failures == 0U);
    REQUIRE(counters.invalid_log_headers == 0U);
    REQUIRE(counters.advance_errors == 0U);
    REQUIRE(counters.invalid_records == 2U);
    REQUIRE(counters.missing_schema_metadata == 1U);
    REQUIRE(counters.missing_channel_metadata == 0U);
  }

  SECTION("Corrupted channel record")
  {
    REQUIRE(corrupt_log_file(
      (log_dir / "log_file_000000.olog").string(), *channel2_offset_result + channel_record_header_size, "X"));
    Reader<BufferedReader<TestReaderPolicy>> reader{memory_resource, log_dir.string(), metadata_map_option};
    REQUIRE(reader.open());

    const auto read_result = reader.read_next();
    REQUIRE(read_result);
    const std::string_view missing_channel_prefix = "missing_channel_";
    REQUIRE(read_result->channel_name.substr(0U, missing_channel_prefix.size()) == missing_channel_prefix);
    REQUIRE(read_result->sequence_number == 1U);
    REQUIRE(read_result->log_time == log_time1);
    REQUIRE(read_result->message_time == message_time1);
    REQUIRE(read_result->header.empty());
    REQUIRE(std::ranges::equal(read_result->data, slot.message()));
    REQUIRE(read_result->message_type == LoggedMessageType::regular);

    REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));

    const auto metadata_result = reader.get_channel_metadata(read_result->channel_name);
    REQUIRE(metadata_result);
    REQUIRE(metadata_result->compression_type == CompressionType::none);
    REQUIRE(metadata_result->message_encoding == MessageEncoding::undefined);
    REQUIRE(metadata_result->schema_name.empty());
    REQUIRE(metadata_result->schema_encoding == SchemaEncoding::undefined);
    REQUIRE(metadata_result->schema_definition.empty());

    REQUIRE(reader.get_channel_metadata(channel_name1));
    REQUIRE(reader.get_channel_metadata(channel_name3));
    REQUIRE(reader.get_channel_metadata(channel_name4));

    const auto& counters = reader.get_error_counters();
    REQUIRE(counters.open_failures == 0U);
    REQUIRE(counters.invalid_log_headers == 0U);
    REQUIRE(counters.advance_errors == 0U);
    REQUIRE(counters.invalid_records == 2U);
    REQUIRE(counters.missing_schema_metadata == 0U);
    REQUIRE(counters.missing_channel_metadata == 1U);

    reader.close();
    REQUIRE_FALSE(reader);
  }
}

TEST_CASE("Log messages")
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  static constexpr size_t max_write_mib_per_sec = 100U;
  static constexpr auto max_log_file_duration = std::chrono::seconds{0};
  const auto* log_file_prefix = "log_file_";

  constexpr size_t num_slots = 2U;

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
  const auto channel_type2 = ChannelType::regular;

  const jewels::time::SteadyTime time1{std::chrono::seconds(1)};

  Writer<TestWriterPolicy> writer{
    memory_resource, memory_resource, max_write_mib_per_sec, max_log_file_duration, WriterEnvironment::normal};
  REQUIRE(writer.open_log(log_dir.string(), log_file_prefix, time1));
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

  const auto channel2_offset_result = writer.get_log_file_offset();
  REQUIRE(channel2_offset_result);

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

  REQUIRE(writer.drain_async_operations());

  std::vector<std::array<std::byte, message_data_size>> message_buffers;

  constexpr LogTimestamp log_time1{std::chrono::nanoseconds(100)};
  constexpr LogTimestamp message_time1{std::chrono::nanoseconds(1000)};
  constexpr auto message_interval = std::chrono::nanoseconds(10);

  for (size_t i = 0; i < num_slots; ++i)
  {
    REQUIRE(pinion_buffer.increment_head(i, 1UL));
  }

  size_t message1_offset{};
  const size_t messages_per_file = 20U;
  auto log_time = log_time1;
  auto message_time = message_time1;
  message_buffers.reserve(messages_per_file * 2U);
  for (size_t index = 0U; index < messages_per_file * 2U; ++index)
  {
    REQUIRE(pinion_buffer.increment_tail(index, 1UL));
    auto buffer_iterator = std::end(pinion_buffer);
    REQUIRE(pinion_buffer.increment_head(index + num_slots, 1UL));
    auto slot = buffer_iterator.dereference();

    fill_with_random_bytes(slot.message());
    message_buffers.emplace_back();
    std::memcpy(message_buffers.back().data(), slot.message().data(), message_data_size);

    slot.header()->sequence_number = index + 1U;
    slot.header()->publish_timestamp = message_time.get_nanoseconds();

    if (index == 0U)
    {
      const auto offset_result = writer.get_log_file_offset();
      REQUIRE(offset_result);
      message1_offset = jewels::Aligner<TestWriterPolicy::alignment>::align_next(*offset_result);
    }

    REQUIRE(writer.log_clockwork_message_wait(
      index % 2U == 0 ? channel_name1 : channel_name2,
      ClockworkMessageHandle{jewels::memory::make_non_null_from_ref(pinion_buffer), buffer_iterator},
      log_time,
      time1));
    REQUIRE(writer.drain_async_operations());
    log_time += message_interval;
    message_time += message_interval;
  }

  writer.periodic_callback(time1);
  REQUIRE(writer.close_log(time1));
  REQUIRE(writer.drain_async_operations());

  SECTION("List log files")
  {
    const auto list_result =
      Reader<BufferedReader<TestReaderPolicy>>::list_log_files(memory_resource, log_dir.string());
    REQUIRE(list_result);
    REQUIRE(list_result->size() == 2U);
    REQUIRE(list_result->front() == std::string_view{(log_dir / "log_file_000000.olog").string()});
    REQUIRE(list_result->back() == std::string_view{(log_dir / "log_file_000001.olog").string()});
  }

  SECTION("Get file log interval")
  {
    auto interval_result = Reader<BufferedReader<TestReaderPolicy>>::get_file_log_interval(
      memory_resource, (log_dir / "log_file_000000.olog").string(), TimeFilterOption::log_time);
    REQUIRE(interval_result);
    REQUIRE(interval_result->get_start_timestamp() == log_time1);
    REQUIRE(interval_result->get_end_timestamp() == log_time1 + (message_interval * messages_per_file));
    interval_result = Reader<BufferedReader<TestReaderPolicy>>::get_file_log_interval(
      memory_resource, (log_dir / "log_file_000000.olog").string(), TimeFilterOption::message_time);
    REQUIRE(interval_result);
    REQUIRE(interval_result->get_start_timestamp() == message_time1);
    REQUIRE(interval_result->get_end_timestamp() == message_time1 + (message_interval * messages_per_file));
    interval_result = Reader<BufferedReader<TestReaderPolicy>>::get_file_log_interval(
      memory_resource, (log_dir / "log_file_000001.olog").string(), TimeFilterOption::log_time);
    REQUIRE(interval_result);
    REQUIRE(interval_result->get_start_timestamp() == log_time1 + (message_interval * (messages_per_file + 1U)));
    REQUIRE(interval_result->get_end_timestamp() == log_time1 + (message_interval * ((messages_per_file * 2U) - 1U)));
    interval_result = Reader<BufferedReader<TestReaderPolicy>>::get_file_log_interval(
      memory_resource, (log_dir / "log_file_000001.olog").string(), TimeFilterOption::message_time);
    REQUIRE(interval_result);
    REQUIRE(interval_result->get_start_timestamp() == message_time1 + (message_interval * (messages_per_file + 1U)));
    REQUIRE(
      interval_result->get_end_timestamp() == message_time1 + (message_interval * ((messages_per_file * 2U) - 1U)));
    REQUIRE_FALSE(
      Reader<BufferedReader<TestReaderPolicy>>::get_file_log_interval(
        memory_resource, (log_dir / "log_file_000002.olog").string(), TimeFilterOption::log_time));
  }

  SECTION("Get file log interval, no end log record")
  {
    jewels::filesystem::Filesystem filesys{memory_resource};
    const auto size_result = filesys.get_size((log_dir / "log_file_000000.olog").string());
    REQUIRE(size_result);
    REQUIRE(corrupt_log_file((log_dir / "log_file_000000.olog").string(), *size_result - 2U, "X"));
    auto interval_result = Reader<BufferedReader<TestReaderPolicy>>::get_file_log_interval(
      memory_resource, (log_dir / "log_file_000000.olog").string(), TimeFilterOption::log_time);
    REQUIRE(interval_result);
    REQUIRE(interval_result->get_start_timestamp() == log_time1);
    REQUIRE(interval_result->get_end_timestamp() == log_time1 + (message_interval * messages_per_file));
    interval_result = Reader<BufferedReader<TestReaderPolicy>>::get_file_log_interval(
      memory_resource, (log_dir / "log_file_000000.olog").string(), TimeFilterOption::message_time);
    REQUIRE(interval_result);
    REQUIRE(interval_result->get_start_timestamp() == message_time1);
    REQUIRE(interval_result->get_end_timestamp() == message_time1 + (message_interval * messages_per_file));
  }

  SECTION("Read logged messages")
  {
    Reader<BufferedReader<TestReaderPolicy>> reader{memory_resource, log_dir.string(), MetadataMapOption::disable};
    REQUIRE(reader.open());

    log_time = log_time1;
    message_time = message_time1;
    for (uint32_t index = 0U; index < messages_per_file * 2U; ++index)
    {
      const auto& message_buffer = message_buffers.at(index);

      const auto read_result = reader.read_next();
      REQUIRE(read_result);
      REQUIRE(read_result->channel_name == ((index % 2U) == 0 ? channel_name1 : channel_name2));
      REQUIRE(read_result->sequence_number == index + 1U);
      REQUIRE(read_result->log_time == log_time);
      REQUIRE(read_result->message_time == message_time);
      REQUIRE(read_result->header.empty());
      REQUIRE(std::ranges::equal(read_result->data, message_buffer));
      REQUIRE(read_result->message_type == LoggedMessageType::regular);
      log_time += message_interval;
      message_time += message_interval;
    }

    REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));

    auto metadata_result = reader.get_channel_metadata(channel_name1);
    REQUIRE(metadata_result);
    REQUIRE(metadata_result->compression_type == compression_type1);
    REQUIRE(metadata_result->message_encoding == message_encoding1);
    REQUIRE(metadata_result->channel_type == channel_type1);
    REQUIRE(metadata_result->schema_name == schema_name1);
    REQUIRE(metadata_result->schema_encoding == schema_encoding1);
    REQUIRE(metadata_result->schema_definition == schema_definition1);

    metadata_result = reader.get_channel_metadata(channel_name2);
    REQUIRE(metadata_result);
    REQUIRE(metadata_result->compression_type == compression_type2);
    REQUIRE(metadata_result->message_encoding == message_encoding2);
    REQUIRE(metadata_result->channel_type == channel_type2);
    REQUIRE(metadata_result->schema_name == schema_name1);
    REQUIRE(metadata_result->schema_encoding == schema_encoding1);
    REQUIRE(metadata_result->schema_definition == schema_definition1);

    const auto& counters = reader.get_error_counters();
    REQUIRE(counters.open_failures == 0U);
    REQUIRE(counters.invalid_log_headers == 0U);
    REQUIRE(counters.advance_errors == 0U);
    REQUIRE(counters.invalid_records == 0U);
    REQUIRE(counters.missing_schema_metadata == 0U);
    REQUIRE(counters.missing_channel_metadata == 0U);

    reader.close();
    REQUIRE_FALSE(reader);
  }

  SECTION("Read with log interval")
  {
    constexpr uint32_t messages_to_skip = messages_per_file / 2U;
    constexpr LogInterval log_interval{
      {log_time1 + (message_interval * messages_to_skip)},
      {log_time1 + (message_interval * (messages_to_skip + messages_per_file - 1U))}};
    const auto list_result =
      Reader<BufferedReader<TestReaderPolicy>>::list_log_files(memory_resource, log_dir.string());
    REQUIRE(list_result);
    Reader<BufferedReader<TestReaderPolicy>> reader{memory_resource, list_result.value(), MetadataMapOption::disable};
    REQUIRE(reader.open(log_interval));

    log_time = log_time1 + (message_interval * messages_to_skip);
    ;
    message_time = message_time1 + (message_interval * messages_to_skip);
    ;
    for (uint32_t index = messages_to_skip; index < messages_to_skip + messages_per_file; ++index)
    {
      const auto& message_data = message_buffers.at(index);

      const auto read_result = reader.read_next();
      REQUIRE(read_result);
      REQUIRE(read_result->channel_name == ((index % 2U) == 0 ? channel_name1 : channel_name2));
      REQUIRE(read_result->sequence_number == index + 1U);
      REQUIRE(read_result->log_time == log_time);
      REQUIRE(read_result->message_time == message_time);
      REQUIRE(read_result->header.empty());
      REQUIRE(std::ranges::equal(read_result->data, message_data));
      REQUIRE(read_result->message_type == LoggedMessageType::regular);
      log_time += message_interval;
      message_time += message_interval;
    }

    REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));

    auto metadata_result = reader.get_channel_metadata(channel_name1);
    REQUIRE(metadata_result);
    REQUIRE(metadata_result->compression_type == compression_type1);
    REQUIRE(metadata_result->message_encoding == message_encoding1);
    REQUIRE(metadata_result->channel_type == channel_type1);
    REQUIRE(metadata_result->schema_name == schema_name1);
    REQUIRE(metadata_result->schema_encoding == schema_encoding1);
    REQUIRE(metadata_result->schema_definition == schema_definition1);

    metadata_result = reader.get_channel_metadata(channel_name2);
    REQUIRE(metadata_result);
    REQUIRE(metadata_result->compression_type == compression_type2);
    REQUIRE(metadata_result->message_encoding == message_encoding2);
    REQUIRE(metadata_result->channel_type == channel_type2);
    REQUIRE(metadata_result->schema_name == schema_name1);
    REQUIRE(metadata_result->schema_encoding == schema_encoding1);
    REQUIRE(metadata_result->schema_definition == schema_definition1);

    const auto& counters = reader.get_error_counters();
    REQUIRE(counters.open_failures == 0U);
    REQUIRE(counters.invalid_log_headers == 0U);
    REQUIRE(counters.advance_errors == 0U);
    REQUIRE(counters.invalid_records == 0U);
    REQUIRE(counters.missing_schema_metadata == 0U);
    REQUIRE(counters.missing_channel_metadata == 0U);

    reader.close();
    REQUIRE_FALSE(reader);
  }

  SECTION("Corrupted log message")
  {
    REQUIRE(
      corrupt_log_file((log_dir / "log_file_000000.olog").string(), message1_offset + message_record_header_size, "X"));
    Reader<BufferedReader<TestReaderPolicy>> reader{memory_resource, log_dir.string(), MetadataMapOption::disable};
    REQUIRE(reader.open());

    log_time = log_time1;
    message_time = message_time1;
    for (uint32_t index = 1U; index < messages_per_file * 2U; ++index)
    {
      log_time += message_interval;
      message_time += message_interval;

      const auto& message_data = message_buffers.at(index);

      const auto read_result = reader.read_next();
      REQUIRE(read_result);
      REQUIRE(read_result->channel_name == ((index % 2U) == 0 ? channel_name1 : channel_name2));
      REQUIRE(read_result->sequence_number == index + 1U);
      REQUIRE(read_result->log_time == log_time);
      REQUIRE(read_result->message_time == message_time);
      REQUIRE(read_result->header.empty());
      REQUIRE(std::ranges::equal(read_result->data, message_data));
      REQUIRE(read_result->message_type == LoggedMessageType::regular);
    }

    REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));

    auto metadata_result = reader.get_channel_metadata(channel_name1);
    REQUIRE(metadata_result);
    REQUIRE(metadata_result->compression_type == compression_type1);
    REQUIRE(metadata_result->message_encoding == message_encoding1);
    REQUIRE(metadata_result->channel_type == channel_type1);
    REQUIRE(metadata_result->schema_name == schema_name1);
    REQUIRE(metadata_result->schema_encoding == schema_encoding1);
    REQUIRE(metadata_result->schema_definition == schema_definition1);

    metadata_result = reader.get_channel_metadata(channel_name2);
    REQUIRE(metadata_result);
    REQUIRE(metadata_result->compression_type == compression_type2);
    REQUIRE(metadata_result->message_encoding == message_encoding2);
    REQUIRE(metadata_result->channel_type == channel_type2);
    REQUIRE(metadata_result->schema_name == schema_name1);
    REQUIRE(metadata_result->schema_encoding == schema_encoding1);
    REQUIRE(metadata_result->schema_definition == schema_definition1);

    const auto& counters = reader.get_error_counters();
    REQUIRE(counters.open_failures == 0U);
    REQUIRE(counters.invalid_log_headers == 0U);
    REQUIRE(counters.advance_errors == 0U);
    REQUIRE(counters.invalid_records == 1U);
    REQUIRE(counters.missing_schema_metadata == 0U);
    REQUIRE(counters.missing_channel_metadata == 0U);

    reader.close();
    REQUIRE_FALSE(reader);
  }

  SECTION("Corrupted channel record recovered by redundant metadata")
  {
    REQUIRE(corrupt_log_file((log_dir / "log_file_000000.olog").string(), *channel2_offset_result, "X"));
    Reader<BufferedReader<TestReaderPolicy>> reader{memory_resource, log_dir.string(), MetadataMapOption::disable};
    REQUIRE(reader.open());

    message_time = message_time1;
    for (uint32_t index = 0U; index < messages_per_file * 2U; ++index)
    {
      const auto read_result = reader.read_next();
      REQUIRE(read_result);
      REQUIRE(read_result->channel_name == ((index % 2U) == 0 ? channel_name1 : channel_name2));
    }

    REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));

    REQUIRE(reader.get_channel_metadata(channel_name1));
    REQUIRE(reader.get_channel_metadata(channel_name2));

    const auto& counters = reader.get_error_counters();
    REQUIRE(counters.open_failures == 0U);
    REQUIRE(counters.invalid_log_headers == 0U);
    REQUIRE(counters.advance_errors == 0U);
    REQUIRE(counters.invalid_records == 2U);
    REQUIRE(counters.missing_schema_metadata == 0U);
    REQUIRE(counters.missing_channel_metadata == 0U);

    reader.close();
    REQUIRE_FALSE(reader);
  }
}

} // namespace
} // namespace clockwork_logging::onboard::tests
