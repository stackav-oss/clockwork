// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/channel_type_clk_cc.hh"
#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/message_encoding_clk_cc.hh"
#include "clockwork/logging/offboard/s3_utils.hh"
#include "clockwork/logging/offboard/s3_utils_interface.hh"
#include "clockwork/logging/onboard/async_write_request.hh"
#include "clockwork/logging/onboard/async_writer.hh"
#include "clockwork/logging/onboard/buffered_reader.hh"
#include "clockwork/logging/onboard/log_format.hh"
#include "clockwork/logging/onboard/offboard_buffered_reader.hh"
#include "clockwork/logging/onboard/reader.hh"
#include "clockwork/logging/onboard/tests/support/test_message_handle.hh"
#include "clockwork/logging/onboard/tests/support/test_support.hh"
#include "clockwork/logging/onboard/types.hh"
#include "clockwork/logging/onboard/writer.hh"
#include "clockwork/logging/schema_encoding_clk_cc.hh"
#include "jewels/aligner/aligner.hh"
#include "jewels/container/circular_buffer.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file_descriptor.hh"
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
#include "jewels/testing/filesystem_wrapper.hh"
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/time/sync_time.hh"

#include <catch2/catch_message.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <fmt/format.h>
#include <gsl/util>

#include <algorithm>
#include <chrono>
#include <compare>
#include <cstdint>
#include <cstring>
#include <memory_resource>
#include <ratio>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace clockwork_logging::onboard::tests
{
namespace
{

/// Message buffer size
constexpr size_t message_buffer_size = 12U * jewels::math::constants::bytes_per_kib<size_t>;

/// Message buffer alignment
constexpr size_t message_alignment = 4U * jewels::math::constants::bytes_per_kib<size_t>;

using TestMessageBufferType = jewels::SharedBufferPool<message_buffer_size, message_alignment>::SharedReference;

using TestMessageHandleType = TestMessageHandle<TestMessageBufferType>;

struct TestWriterPolicy
{
  /// Data buffer size
  static constexpr size_t buffer_size = 64U * jewels::math::constants::bytes_per_kib<size_t>;

  /// Data buffer alignment
  static constexpr size_t alignment = 4U * jewels::math::constants::bytes_per_kib<size_t>;

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

  /// Minimum zero copy message size
  static constexpr size_t min_zero_copy_message_size = 4U * jewels::math::constants::bytes_per_kib<size_t>;

  /// Maximum log file size
  static constexpr size_t max_log_file_size = 256U * jewels::math::constants::bytes_per_kib<size_t>;

  /// Message handle type
  using MessageHandleType = TestMessageHandleType;

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
  /// S3 utilities library type
  using S3UtilsType = offboard::S3Utils;

  /// Filesystem library type
  using FilesystemType = jewels::filesystem::testing::FilesystemWrapper;

  /// Read buffer size
  static constexpr size_t read_buffer_size = 2U * jewels::math::constants::bytes_per_mib<size_t>;

  /// Maximum read size
  static constexpr size_t max_read_size = max_log_record_size;

  /// Minimum size of reads when recovering from I/O error
  static constexpr size_t min_io_error_recover_read_size = 512U;
};

TEMPLATE_TEST_CASE("Read metadata", "", BufferedReader<TestReaderPolicy>, OffboardBufferedReader<TestReaderPolicy>)
{
  using BufferedReaderType = TestType;

  const auto metadata_map_option = GENERATE(MetadataMapOption::enable, MetadataMapOption::disable);
  CAPTURE(metadata_map_option);

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  static constexpr size_t header_size = 29U;
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

  jewels::SharedBufferPool<message_buffer_size, message_alignment> message_buffer_pool{memory_resource, 1U};

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

  auto message_result = message_buffer_pool.get_shared_buffer();
  REQUIRE(message_result);
  auto message_buffer = std::move(message_result).value();
  auto message_handle = TestMessageHandleType{message_buffer};
  const auto message_data = std::span{*message_buffer};
  fill_with_random_bytes(message_data);

  constexpr auto log_time1 = LogTimestamp(std::chrono::nanoseconds(10));
  constexpr auto message_time1 = LogTimestamp(std::chrono::nanoseconds(100));

  REQUIRE(writer.log_message_wait(
    Message{
      .channel_name = channel_name2,
      .sequence_number = 1U,
      .log_time = log_time1,
      .message_time = message_time1,
      .header = message_data.first(header_size),
      .data = message_data.last(message_data.size() - header_size),
    },
    message_handle,
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

  const auto buffered_reader = tests::make_buffered_reader<BufferedReaderType>();

  SECTION("Get logged metadata")
  {
    Reader<BufferedReaderType> reader{memory_resource, log_dir.string(), buffered_reader, metadata_map_option};
    REQUIRE_FALSE(reader);
    REQUIRE(reader.open());

    const auto read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);
    REQUIRE(read_result->sequence_number == 1U);
    REQUIRE(read_result->log_time == log_time1);
    REQUIRE(read_result->message_time == message_time1);
    REQUIRE(std::ranges::equal(read_result->header, message_data.first(header_size)));
    REQUIRE(std::ranges::equal(read_result->data, message_data.last(message_data.size() - header_size)));
    REQUIRE(read_result->message_type == LoggedMessageType::regular);
    REQUIRE(read_result->message_encoding == message_encoding2);

    REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));

    auto metadata_result = reader.get_channel_metadata(channel_name1);
    REQUIRE(metadata_result);
    REQUIRE(metadata_result->compression_type == compression_type1);
    REQUIRE(metadata_result->message_encoding == message_encoding1);
    REQUIRE(metadata_result->channel_type == channel_type1);
    REQUIRE(metadata_result->schema_name == schema_name1);
    REQUIRE(metadata_result->schema_encoding == schema_encoding1);
    REQUIRE(metadata_result->schema_definition == schema_definition1);
    REQUIRE(read_result->message_encoding == message_encoding1);

    metadata_result = reader.get_channel_metadata(channel_name2);
    REQUIRE(metadata_result);
    REQUIRE(metadata_result->compression_type == compression_type2);
    REQUIRE(metadata_result->message_encoding == message_encoding2);
    REQUIRE(metadata_result->channel_type == channel_type2);
    REQUIRE(metadata_result->schema_name == schema_name1);
    REQUIRE(metadata_result->schema_encoding == schema_encoding1);
    REQUIRE(metadata_result->schema_definition == schema_definition1);
    REQUIRE(read_result->message_encoding == message_encoding2);

    metadata_result = reader.get_channel_metadata(channel_name3);
    REQUIRE(metadata_result);
    REQUIRE(metadata_result->compression_type == compression_type3);
    REQUIRE(metadata_result->message_encoding == message_encoding3);
    REQUIRE(metadata_result->channel_type == channel_type3);
    REQUIRE(metadata_result->schema_name == schema_name2);
    REQUIRE(metadata_result->schema_encoding == schema_encoding2);
    REQUIRE(metadata_result->schema_definition == schema_definition2);
    REQUIRE(read_result->message_encoding == message_encoding3);

    metadata_result = reader.get_channel_metadata(channel_name4);
    REQUIRE(metadata_result);
    REQUIRE(metadata_result->compression_type == compression_type4);
    REQUIRE(metadata_result->message_encoding == message_encoding4);
    REQUIRE(metadata_result->channel_type == channel_type4);
    REQUIRE(metadata_result->schema_name == schema_name2);
    REQUIRE(metadata_result->schema_encoding == schema_encoding2);
    REQUIRE(metadata_result->schema_definition == schema_definition2);
    REQUIRE(read_result->message_encoding == message_encoding4);

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
    Reader<BufferedReaderType> reader{memory_resource, log_dir.string(), buffered_reader, metadata_map_option};
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
    Reader<BufferedReaderType> reader{memory_resource, log_dir.string(), buffered_reader, metadata_map_option};
    REQUIRE(reader.open());

    const auto read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);
    REQUIRE(read_result->message_encoding == message_encoding2);

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
    Reader<BufferedReaderType> reader{memory_resource, log_dir.string(), buffered_reader, metadata_map_option};
    REQUIRE(reader.open());

    const auto read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);
    REQUIRE(read_result->message_encoding == message_encoding2);

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
    Reader<BufferedReaderType> reader{memory_resource, log_dir.string(), buffered_reader, metadata_map_option};
    REQUIRE(reader.open());

    const auto read_result = reader.read_next();
    REQUIRE(read_result);
    const std::string_view missing_channel_prefix = "missing_channel_";
    REQUIRE(read_result->channel_name.substr(0U, missing_channel_prefix.size()) == missing_channel_prefix);
    REQUIRE(read_result->sequence_number == 1U);
    REQUIRE(read_result->log_time == log_time1);
    REQUIRE(read_result->message_time == message_time1);
    REQUIRE(read_result->message_encoding == MessageEncoding::undefined);
    REQUIRE(std::ranges::equal(read_result->header, message_data.first(header_size)));
    REQUIRE(std::ranges::equal(read_result->data, message_data.last(message_data.size() - header_size)));

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

TEMPLATE_TEST_CASE("Log messages", "", BufferedReader<TestReaderPolicy>, OffboardBufferedReader<TestReaderPolicy>)
{
  using BufferedReaderType = TestType;

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  static constexpr size_t header_size = 29U;
  static constexpr size_t max_write_mib_per_sec = 100U;
  static constexpr auto max_log_file_duration = std::chrono::seconds{0};

  static constexpr size_t message_buffer_count = 100U;
  const auto* log_file_prefix = "log_file_";

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

  jewels::SharedBufferPool<message_buffer_size, message_alignment> message_buffer_pool{
    memory_resource, message_buffer_count};

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

  std::vector<TestMessageBufferType> message_buffers;

  constexpr LogTimestamp log_time1{std::chrono::nanoseconds(100)};
  constexpr LogTimestamp message_time1{std::chrono::nanoseconds(1000)};
  constexpr auto message_interval = std::chrono::nanoseconds(10);

  size_t message1_offset{};
  const uint32_t messages_per_file = 15U;
  auto log_time = log_time1;
  auto message_time = message_time1;
  message_buffers.reserve(static_cast<size_t>(messages_per_file) * 2U);
  for (uint32_t sequence_number = 1U; sequence_number <= messages_per_file * 2U; ++sequence_number)
  {
    auto message_result = message_buffer_pool.get_shared_buffer();
    REQUIRE(message_result);
    auto message_buffer = std::move(message_result).value();
    message_buffers.push_back(message_buffer);
    auto message_handle = TestMessageHandleType{message_buffer};
    const auto message_data = std::span{*message_buffer};
    fill_with_random_bytes(message_data);

    if (sequence_number == 1U)
    {
      const auto offset_result = writer.get_log_file_offset();
      REQUIRE(offset_result);
      message1_offset = jewels::Aligner<TestWriterPolicy::alignment>::align_next(*offset_result);
    }

    REQUIRE(writer.log_message_wait(
      Message{
        .channel_name = sequence_number % 2U == 0 ? channel_name1 : channel_name2,
        .sequence_number = sequence_number,
        .log_time = log_time,
        .message_time = message_time,
        .header = message_data.first(header_size),
        .data = message_data.last(message_data.size() - header_size),
      },
      message_handle,
      time1));
    log_time += message_interval;
    message_time += message_interval;
  }

  writer.periodic_callback(time1);
  REQUIRE(writer.close_log(time1));
  REQUIRE(writer.drain_async_operations());

  const auto buffered_reader = tests::make_buffered_reader<BufferedReaderType>();

  SECTION("List log files")
  {
    const auto list_result = buffered_reader->list_log_files(log_dir.string());
    REQUIRE(list_result);
    REQUIRE(list_result->size() == 2U);
    REQUIRE(list_result->front() == std::string_view{(log_dir / "log_file_000000.olog").string()});
    REQUIRE(list_result->back() == std::string_view{(log_dir / "log_file_000001.olog").string()});
  }

  jewels::filesystem::Filesystem filesys{memory_resource};
  const auto size_result1 = filesys.get_size((log_dir / "log_file_000000.olog").string());
  REQUIRE(size_result1);
  const auto file_size1 = size_result1.value();
  const auto size_result2 = filesys.get_size((log_dir / "log_file_000001.olog").string());
  REQUIRE(size_result2);
  const auto file_size2 = size_result2.value();

  SECTION("Get file log interval")
  {
    auto interval_result = Reader<BufferedReaderType>::get_file_log_interval(
      memory_resource, (log_dir / "log_file_000000.olog").string(), TimeFilterOption::log_time, buffered_reader);
    REQUIRE(interval_result);
    REQUIRE(interval_result->get_start_timestamp() == log_time1);
    REQUIRE(interval_result->get_end_timestamp() == log_time1 + (message_interval * messages_per_file));
    interval_result = Reader<BufferedReaderType>::get_file_log_interval(
      memory_resource, (log_dir / "log_file_000000.olog").string(), TimeFilterOption::message_time, buffered_reader);
    REQUIRE(interval_result);
    REQUIRE(interval_result->get_start_timestamp() == message_time1);
    REQUIRE(interval_result->get_end_timestamp() == message_time1 + (message_interval * messages_per_file));
    interval_result = Reader<BufferedReaderType>::get_file_log_interval(
      memory_resource, (log_dir / "log_file_000001.olog").string(), TimeFilterOption::log_time, buffered_reader);
    REQUIRE(interval_result);
    REQUIRE(interval_result->get_start_timestamp() == log_time1 + (message_interval * (messages_per_file + 1U)));
    REQUIRE(interval_result->get_end_timestamp() == log_time1 + (message_interval * ((messages_per_file * 2U) - 1U)));
    interval_result = Reader<BufferedReaderType>::get_file_log_interval(
      memory_resource, (log_dir / "log_file_000001.olog").string(), TimeFilterOption::message_time, buffered_reader);
    REQUIRE(interval_result);
    REQUIRE(interval_result->get_start_timestamp() == message_time1 + (message_interval * (messages_per_file + 1U)));
    REQUIRE(
      interval_result->get_end_timestamp() == message_time1 + (message_interval * ((messages_per_file * 2U) - 1U)));
    REQUIRE_FALSE(
      Reader<BufferedReaderType>::get_file_log_interval(
        memory_resource, (log_dir / "log_file_000002.olog").string(), TimeFilterOption::log_time, buffered_reader));
  }

  SECTION("Get file log interval, no end log record")
  {
    REQUIRE(corrupt_log_file((log_dir / "log_file_000000.olog").string(), file_size1 - 2U, "X"));
    auto interval_result = Reader<BufferedReaderType>::get_file_log_interval(
      memory_resource, (log_dir / "log_file_000000.olog").string(), TimeFilterOption::log_time, buffered_reader);
    REQUIRE(interval_result);
    REQUIRE(interval_result->get_start_timestamp() == log_time1);
    REQUIRE(interval_result->get_end_timestamp() == log_time1 + (message_interval * messages_per_file));
    interval_result = Reader<BufferedReaderType>::get_file_log_interval(
      memory_resource, (log_dir / "log_file_000000.olog").string(), TimeFilterOption::message_time, buffered_reader);
    REQUIRE(interval_result);
    REQUIRE(interval_result->get_start_timestamp() == message_time1);
    REQUIRE(interval_result->get_end_timestamp() == message_time1 + (message_interval * messages_per_file));
  }

  SECTION("Read logged messages")
  {
    Reader<BufferedReaderType> reader{memory_resource, log_dir.string(), buffered_reader, MetadataMapOption::disable};
    const auto start_time = jewels::time::SteadyClock::now();
    REQUIRE(reader.open());

    log_time = log_time1;
    message_time = message_time1;
    for (uint32_t sequence_number = 1U; sequence_number <= messages_per_file * 2U; ++sequence_number)
    {
      const auto& message_buffer = message_buffers.at(sequence_number - 1U);
      const auto message_data = std::span{*message_buffer};

      const auto read_result = reader.read_next();
      REQUIRE(read_result);
      REQUIRE(read_result->channel_name == ((sequence_number % 2U) == 0 ? channel_name1 : channel_name2));
      REQUIRE(read_result->sequence_number == sequence_number);
      REQUIRE(read_result->log_time == log_time);
      REQUIRE(read_result->message_time == message_time);
      REQUIRE(std::ranges::equal(read_result->header, message_data.first(header_size)));
      REQUIRE(std::ranges::equal(read_result->data, message_data.last(message_data.size() - header_size)));
      REQUIRE(read_result->message_type == LoggedMessageType::regular);
      REQUIRE(read_result->message_encoding == ((sequence_number % 2U) == 0 ? message_encoding1 : message_encoding2));
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
    const auto end_time = jewels::time::SteadyClock::now();
    REQUIRE_FALSE(reader);

    const auto read_metrics = reader.buffered_reader().get_read_metrics();
    REQUIRE(read_metrics.byte_count >= file_size1 + file_size2);
    REQUIRE(read_metrics.read_count != 0U);
    REQUIRE(read_metrics.read_latency > std::chrono::nanoseconds(0));
    REQUIRE(read_metrics.read_latency <= end_time - start_time);
  }

  SECTION("Read with log interval")
  {
    constexpr uint32_t messages_to_skip = messages_per_file / 2U;
    constexpr LogInterval log_interval{
      {log_time1 + (message_interval * messages_to_skip)},
      {log_time1 + (message_interval * (messages_to_skip + messages_per_file - 1U))}};
    const auto list_result = buffered_reader->list_log_files(log_dir.string());
    REQUIRE(list_result);
    Reader<BufferedReaderType> reader{
      memory_resource, list_result.value(), buffered_reader, MetadataMapOption::disable};
    REQUIRE(reader.open(log_interval));

    log_time = log_time1 + (message_interval * messages_to_skip);
    ;
    message_time = message_time1 + (message_interval * messages_to_skip);
    ;
    for (uint32_t sequence_number = messages_to_skip + 1U; sequence_number <= messages_to_skip + messages_per_file;
         ++sequence_number)
    {
      const auto& message_buffer = message_buffers.at(sequence_number - 1U);
      const auto message_data = std::span{*message_buffer};

      const auto read_result = reader.read_next();
      REQUIRE(read_result);
      REQUIRE(read_result->channel_name == ((sequence_number % 2U) == 0 ? channel_name1 : channel_name2));
      REQUIRE(read_result->sequence_number == sequence_number);
      REQUIRE(read_result->log_time == log_time);
      REQUIRE(read_result->message_time == message_time);
      REQUIRE(std::ranges::equal(read_result->header, message_data.first(header_size)));
      REQUIRE(std::ranges::equal(read_result->data, message_data.last(message_data.size() - header_size)));
      REQUIRE(read_result->message_type == LoggedMessageType::regular);
      REQUIRE(read_result->message_encoding == ((sequence_number % 2U) == 0 ? message_encoding1 : message_encoding2));
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
    Reader<BufferedReaderType> reader{memory_resource, log_dir.string(), buffered_reader, MetadataMapOption::disable};
    REQUIRE(reader.open());

    log_time = log_time1;
    message_time = message_time1;
    for (uint32_t sequence_number = 2U; sequence_number <= messages_per_file * 2U; ++sequence_number)
    {
      log_time += message_interval;
      message_time += message_interval;

      const auto& message_buffer = message_buffers.at(sequence_number - 1U);
      const auto message_data = std::span{*message_buffer};

      const auto read_result = reader.read_next();
      REQUIRE(read_result);
      REQUIRE(read_result->channel_name == ((sequence_number % 2U) == 0 ? channel_name1 : channel_name2));
      REQUIRE(read_result->sequence_number == sequence_number);
      REQUIRE(read_result->log_time == log_time);
      REQUIRE(read_result->message_time == message_time);
      REQUIRE(std::ranges::equal(read_result->header, message_data.first(header_size)));
      REQUIRE(std::ranges::equal(read_result->data, message_data.last(message_data.size() - header_size)));
      REQUIRE(read_result->message_type == LoggedMessageType::regular);
      REQUIRE(read_result->message_encoding == ((sequence_number % 2U) == 0 ? message_encoding1 : message_encoding2));
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
    Reader<BufferedReaderType> reader{memory_resource, log_dir.string(), buffered_reader, MetadataMapOption::disable};
    REQUIRE(reader.open());

    log_time = log_time1;
    message_time = message_time1;
    for (uint32_t sequence_number = 1U; sequence_number <= messages_per_file * 2U; ++sequence_number)
    {
      const auto read_result = reader.read_next();
      REQUIRE(read_result);
      REQUIRE(read_result->channel_name == ((sequence_number % 2U) == 0 ? channel_name1 : channel_name2));
      REQUIRE(read_result->message_encoding == ((sequence_number % 2U) == 0 ? message_encoding1 : message_encoding2));
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

TEMPLATE_TEST_CASE(
  "List log files for interval", "", BufferedReader<TestReaderPolicy>, OffboardBufferedReader<TestReaderPolicy>)
{
  using BufferedReaderType = TestType;

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  static constexpr size_t max_write_mib_per_sec = 100U;
  static constexpr auto max_log_file_duration = std::chrono::seconds{0};

  static constexpr size_t message_buffer_count = 1U;
  const auto* log_file_prefix = "log_file_";

  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto log_dir = test_dir.get_path() / log_file_prefix;

  const auto* schema_name1 = "Schema 1";
  const auto schema_encoding1 = SchemaEncoding::unspecified;
  const auto* schema_definition1 = "";

  const auto* channel_name1 = "Channel 1";
  const auto compression_type1 = CompressionType::none;
  const auto message_encoding1 = MessageEncoding::unspecified;
  const auto channel_type1 = ChannelType::regular;

  jewels::SharedBufferPool<message_buffer_size, message_alignment> message_buffer_pool{
    memory_resource, message_buffer_count};
  auto message_result = message_buffer_pool.get_shared_buffer();
  REQUIRE(message_result);
  auto message_buffer = std::move(message_result).value();
  auto message_handle = TestMessageHandleType{message_buffer};
  const auto message_data = std::span{*message_buffer};
  fill_with_random_bytes(message_data);

  const jewels::time::SteadyTime time1{std::chrono::seconds(1)};

  constexpr auto num_log_files = 10U;
  constexpr LogTimestamp log_time0{std::chrono::seconds(1000)};
  constexpr LogTimestamp message_time0{std::chrono::seconds(100000)};
  constexpr auto message_interval = std::chrono::seconds(10);

  const auto buffered_reader = tests::make_buffered_reader<BufferedReaderType>();

  SECTION("Bisect to find log files to read")
  {
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

    const bool last_file_is_empty = GENERATE(true, false);
    CAPTURE(last_file_is_empty);

    auto log_time = log_time0;
    auto message_time = message_time0;
    for (uint32_t sequence_number = 1U; sequence_number <= num_log_files * 2U; ++sequence_number)
    {
      REQUIRE(writer.log_message_wait(
        Message{
          .channel_name = channel_name1,
          .sequence_number = sequence_number,
          .log_time = log_time,
          .message_time = message_time,
          .header = {},
          .data = message_data,
        },
        message_handle,
        time1));
      log_time += message_interval;
      message_time += message_interval;

      if (sequence_number % 2U == 0U && (last_file_is_empty || sequence_number < num_log_files * 2U))
      {
        writer.periodic_callback(time1);
        REQUIRE(writer.split_log(time1));
      }
    }

    writer.periodic_callback(time1);
    REQUIRE(writer.close_log(time1));
    REQUIRE(writer.drain_async_operations());

    SECTION("List log files")
    {
      const auto list_result = buffered_reader->list_log_files(log_dir.string());
      REQUIRE(list_result);
      REQUIRE(list_result->size() == num_log_files + (last_file_is_empty ? 1U : 0U));
      size_t file_index = 0U;
      for (const auto& log_file : list_result.value())
      {
        const auto file_name = fmt::format("{}{:06d}.olog", log_file_prefix, file_index);
        REQUIRE(log_file == (log_dir / file_name).string());
        ++file_index;
      }
    }

    SECTION("List log files for interval that barely covers entire log")
    {
      const auto list_result = Reader<BufferedReaderType>::list_log_files_for_interval(
        memory_resource,
        log_dir.string(),
        LogInterval{log_time0 + message_interval, log_time0 + (message_interval * ((num_log_files * 2) - 2))},
        buffered_reader);
      REQUIRE(list_result);
      REQUIRE(list_result->size() == num_log_files);
      size_t file_index = 0U;
      for (const auto& log_file : list_result.value())
      {
        const auto file_name = fmt::format("{}{:06d}.olog", log_file_prefix, file_index);
        REQUIRE(log_file == (log_dir / file_name).string());
        ++file_index;
      }
    }

    SECTION("List log files for interval that barely covers all but first and last files")
    {
      const auto list_result = Reader<BufferedReaderType>::list_log_files_for_interval(
        memory_resource,
        log_dir.string(),
        LogInterval{log_time0 + (message_interval * 3), log_time0 + (message_interval * ((num_log_files * 2) - 4))},
        buffered_reader);
      REQUIRE(list_result);
      REQUIRE(list_result->size() == num_log_files - 2U);
      size_t file_index = 1U;
      for (const auto& log_file : list_result.value())
      {
        const auto file_name = fmt::format("{}{:06d}.olog", log_file_prefix, file_index);
        REQUIRE(log_file == (log_dir / file_name).string());
        ++file_index;
      }
    }

    SECTION("List log files for interval that just misses the first and last two log files")
    {
      const auto list_result = Reader<BufferedReaderType>::list_log_files_for_interval(
        memory_resource,
        log_dir.string(),
        LogInterval{
          log_time0 + (message_interval * 3) + std::chrono::nanoseconds(1),
          log_time0 + (message_interval * ((num_log_files * 2) - 4)) - std::chrono::nanoseconds(1)},
        buffered_reader);
      REQUIRE(list_result);
      REQUIRE(list_result->size() == num_log_files - 4U);
      size_t file_index = 2U;
      for (const auto& log_file : list_result.value())
      {
        const auto file_name = fmt::format("{}{:06d}.olog", log_file_prefix, file_index);
        REQUIRE(log_file == (log_dir / file_name).string());
        ++file_index;
      }
    }

    SECTION("List log files for interval that barely covers the first half of the log")
    {
      const auto list_result = Reader<BufferedReaderType>::list_log_files_for_interval(
        memory_resource,
        log_dir.string(),
        LogInterval{log_time0 + message_interval, log_time0 + (message_interval * (num_log_files - 2))},
        buffered_reader);
      REQUIRE(list_result);
      REQUIRE(list_result->size() == num_log_files / 2U);
      size_t file_index = 0U;
      for (const auto& log_file : list_result.value())
      {
        const auto file_name = fmt::format("{}{:06d}.olog", log_file_prefix, file_index);
        REQUIRE(log_file == (log_dir / file_name).string());
        ++file_index;
      }
    }

    SECTION("List log files for interval that just misses the first half of the log")
    {
      const auto list_result = Reader<BufferedReaderType>::list_log_files_for_interval(
        memory_resource,
        log_dir.string(),
        LogInterval{
          log_time0 + message_interval + std::chrono::nanoseconds(1),
          log_time0 + (message_interval * (num_log_files - 2)) - std::chrono::nanoseconds(1)},
        buffered_reader);
      REQUIRE(list_result);
      REQUIRE(list_result->size() == (num_log_files / 2U) - 2U);
      size_t file_index = 1U;
      for (const auto& log_file : list_result.value())
      {
        const auto file_name = fmt::format("{}{:06d}.olog", log_file_prefix, file_index);
        REQUIRE(log_file == (log_dir / file_name).string());
        ++file_index;
      }
    }

    SECTION("List log files for interval that barely covers the second half of the log")
    {
      const auto list_result = Reader<BufferedReaderType>::list_log_files_for_interval(
        memory_resource,
        log_dir.string(),
        LogInterval{
          log_time0 + (message_interval * (num_log_files + 1)),
          log_time0 + (message_interval * ((num_log_files * 2) - 2))},
        buffered_reader);
      REQUIRE(list_result);
      REQUIRE(list_result->size() == num_log_files / 2U);
      size_t file_index = num_log_files / 2U;
      for (const auto& log_file : list_result.value())
      {
        const auto file_name = fmt::format("{}{:06d}.olog", log_file_prefix, file_index);
        REQUIRE(log_file == (log_dir / file_name).string());
        ++file_index;
      }
    }

    SECTION("List log files for interval that just misses the second half of the log")
    {
      const auto list_result = Reader<BufferedReaderType>::list_log_files_for_interval(
        memory_resource,
        log_dir.string(),
        LogInterval{
          log_time0 + (message_interval * (num_log_files + 1)) + std::chrono::nanoseconds(1),
          log_time0 + (message_interval * ((num_log_files * 2) - 2)) - std::chrono::nanoseconds(1)},
        buffered_reader);
      REQUIRE(list_result);
      REQUIRE(list_result->size() == (num_log_files / 2U) - 2U);
      size_t file_index = (num_log_files / 2U) + 1U;
      for (const auto& log_file : list_result.value())
      {
        const auto file_name = fmt::format("{}{:06d}.olog", log_file_prefix, file_index);
        REQUIRE(log_file == (log_dir / file_name).string());
        ++file_index;
      }
    }
  }

  SECTION("One log file")
  {
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

    REQUIRE(writer.log_message_wait(
      Message{
        .channel_name = channel_name1,
        .sequence_number = 1U,
        .log_time = log_time0,
        .message_time = message_time0,
        .header = {},
        .data = message_data,
      },
      message_handle,
      time1));

    writer.periodic_callback(time1);
    REQUIRE(writer.close_log(time1));
    REQUIRE(writer.drain_async_operations());

    const auto list_result = Reader<BufferedReaderType>::list_log_files_for_interval(
      memory_resource,
      log_dir.string(),
      LogInterval{log_time0 - std::chrono::nanoseconds(1), log_time0 + std::chrono::nanoseconds(1)},
      buffered_reader);
    REQUIRE(list_result);
    REQUIRE(list_result->size() == 1U);
    REQUIRE(list_result->front() == std::string_view{(log_dir / "log_file_000000.olog").string()});
  }

  SECTION("Two files, last file is empty")
  {
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

    REQUIRE(writer.log_message_wait(
      Message{
        .channel_name = channel_name1,
        .sequence_number = 1U,
        .log_time = log_time0,
        .message_time = message_time0,
        .header = {},
        .data = message_data,
      },
      message_handle,
      time1));

    REQUIRE(writer.split_log(time1));

    writer.periodic_callback(time1);
    REQUIRE(writer.close_log(time1));
    REQUIRE(writer.drain_async_operations());

    SECTION("First file overlaps interval")
    {
      const auto list_result = Reader<BufferedReaderType>::list_log_files_for_interval(
        memory_resource, log_dir.string(), LogInterval{log_time0, log_time0}, buffered_reader);
      REQUIRE(list_result);
      REQUIRE(list_result->size() == 1U);
      REQUIRE(list_result->front() == std::string_view{(log_dir / "log_file_000000.olog").string()});
    }

    SECTION("First file doesn't overlap interval")
    {
      const auto list_result = Reader<BufferedReaderType>::list_log_files_for_interval(
        memory_resource,
        log_dir.string(),
        LogInterval{log_time0 + std::chrono::nanoseconds(1), log_time0 + std::chrono::nanoseconds(1)},
        buffered_reader);
      REQUIRE(list_result);
      REQUIRE(list_result->empty());
    }
  }

  SECTION("Error in first log file")
  {
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

    REQUIRE(writer.split_log(time1));
    REQUIRE(writer.log_message_wait(
      Message{
        .channel_name = channel_name1,
        .sequence_number = 1U,
        .log_time = log_time0,
        .message_time = message_time0,
        .header = {},
        .data = message_data,
      },
      message_handle,
      time1));

    writer.periodic_callback(time1);
    REQUIRE(writer.close_log(time1));
    REQUIRE(writer.drain_async_operations());

    const auto list_result = Reader<BufferedReaderType>::list_log_files_for_interval(
      memory_resource, log_dir.string(), LogInterval{log_time0, log_time0}, buffered_reader);
    REQUIRE(list_result->size() == 1U);
    REQUIRE(list_result->front() == std::string_view{(log_dir / "log_file_000001.olog").string()});
  }

  SECTION("Error in second to last file")
  {
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

    REQUIRE(writer.log_message_wait(
      Message{
        .channel_name = channel_name1,
        .sequence_number = 1U,
        .log_time = log_time0,
        .message_time = message_time0,
        .header = {},
        .data = message_data,
      },
      message_handle,
      time1));

    REQUIRE(writer.split_log(time1));
    REQUIRE(writer.split_log(time1));

    writer.periodic_callback(time1);
    REQUIRE(writer.close_log(time1));
    REQUIRE(writer.drain_async_operations());

    const auto list_result = Reader<BufferedReaderType>::list_log_files_for_interval(
      memory_resource, log_dir.string(), LogInterval{log_time0, log_time0}, buffered_reader);
    REQUIRE(list_result->size() == 1U);
    REQUIRE(list_result->front() == std::string_view{(log_dir / "log_file_000000.olog").string()});
  }
}

} // namespace
} // namespace clockwork_logging::onboard::tests
