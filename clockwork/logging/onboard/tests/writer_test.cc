// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/channel_type_clk_cc.hh"
#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/message_encoding_clk_cc.hh"
#include "clockwork/logging/onboard/async_write_request.hh"
#include "clockwork/logging/onboard/async_writer.hh"
#include "clockwork/logging/onboard/log_format.hh"
#include "clockwork/logging/onboard/tests/support/test_message_handle.hh"
#include "clockwork/logging/onboard/tests/support/test_support.hh"
#include "clockwork/logging/onboard/types.hh"
#include "clockwork/logging/onboard/writer.hh"
#include "clockwork/logging/onboard/writer_state.hh"
#include "clockwork/logging/schema_encoding_clk_cc.hh"
#include "jewels/aligner/aligner.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/math/constants.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/shared_pool/ref_counted_pool.hh"
#include "jewels/shared_pool/shared_buffer_pool.hh"
#include "jewels/shared_pool/shared_object_pool.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/time/sync_time.hh"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace clockwork_logging::onboard
{
namespace
{

/// Message buffer size
constexpr size_t message_buffer_size = 12U * jewels::math::constants::bytes_per_kib<size_t>;

/// Message buffer alignment
constexpr size_t message_alignment = 4U * jewels::math::constants::bytes_per_kib<size_t>;

using TestMessageHandleType =
  TestMessageHandle<jewels::SharedBufferPool<message_buffer_size, message_alignment>::SharedReference>;

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
  static constexpr size_t max_log_file_size = 2U * jewels::math::constants::bytes_per_mib<size_t>;

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

TEST_CASE("Calculate buffer pool size")
{
  SECTION("Zero max data rate and zero max write backlog")
  {
    const size_t max_write_rate_mib = 0U;
    const auto max_write_backlog = std::chrono::nanoseconds(0);
    REQUIRE(Writer<TestWriterPolicy>::calculate_write_buffer_pool_size(max_write_rate_mib, max_write_backlog) == 128U);
  }

  SECTION("4 MiB/sec max data rate and zero max write backlog")
  {
    const size_t max_write_rate_mib = 4U;
    const auto max_write_backlog = std::chrono::nanoseconds(0);
    REQUIRE(Writer<TestWriterPolicy>::calculate_write_buffer_pool_size(max_write_rate_mib, max_write_backlog) == 128U);
  }

  SECTION("4 MiB/sec max data rate and 1 sec max write backlog")
  {
    const size_t max_write_rate_mib = 4U;
    const auto max_write_backlog = std::chrono::seconds(1);
    REQUIRE(Writer<TestWriterPolicy>::calculate_write_buffer_pool_size(max_write_rate_mib, max_write_backlog) == 192U);
  }

  SECTION("4 MiB/sec max data rate and 2 sec max write backlog")
  {
    const size_t max_write_rate_mib = 4U;
    const auto max_write_backlog = std::chrono::seconds(2);
    REQUIRE(Writer<TestWriterPolicy>::calculate_write_buffer_pool_size(max_write_rate_mib, max_write_backlog) == 256U);
  }

  SECTION("8 MiB/sec max data rate and 1 sec max write backlog")
  {
    const size_t max_write_rate_mib = 8U;
    const auto max_write_backlog = std::chrono::seconds(1);
    REQUIRE(Writer<TestWriterPolicy>::calculate_write_buffer_pool_size(max_write_rate_mib, max_write_backlog) == 256U);
  }
}

TEST_CASE("Log metadata")
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  static constexpr size_t max_write_mib_per_sec = 100U;
  static constexpr auto max_log_file_duration = std::chrono::seconds{0};
  const auto* log_file_prefix = "log_file_";

  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto log_dir = test_dir.get_path() / log_file_prefix;

  const auto* schema_name1 = "Schema 1";
  const auto schema_encoding1 = SchemaEncoding::unspecified;
  const auto* schema_desc1 = "Schema description 1";

  const auto* schema_name2 = "Schema 2";
  const auto schema_encoding2 = SchemaEncoding::undefined;
  const auto* schema_desc2 = "";

  const auto* channel_name1 = "Channel 1";
  const auto compression_type1 = CompressionType::none;
  const auto message_encoding1 = MessageEncoding::unspecified;
  const auto channel_type1 = ChannelType::regular;

  const auto* channel_name2 = "Channel 2";
  const auto compression_type2 = CompressionType::none;
  const auto message_encoding2 = MessageEncoding::unspecified;
  const auto channel_type2 = ChannelType::persistent;

  const auto* channel_name3 = "Channel 3";
  const auto compression_type3 = CompressionType::none;
  const auto message_encoding3 = MessageEncoding::unspecified;
  const auto channel_type3 = ChannelType::regular;

  const auto* channel_name4 = "Channel 4";
  const auto compression_type4 = CompressionType::none;
  const auto message_encoding4 = MessageEncoding::unspecified;
  const auto channel_type4 = ChannelType::persistent;

  const jewels::time::SteadyTime time1{std::chrono::seconds(1)};

  std::unordered_map<std::string_view, uint16_t> channel_id_map;
  std::unordered_map<std::string_view, uint16_t> schema_id_map;

  const LoggedChannelMetadata channel_metadata1{
    .channel_name = channel_name1,
    .compression_type = compression_type1,
    .message_encoding = message_encoding1,
    .channel_type = channel_type1,
    .schema_name = schema_name1,
    .schema_encoding = schema_encoding1,
    .schema_definition = schema_desc1,
  };

  const LoggedChannelMetadata channel_metadata2{
    .channel_name = channel_name2,
    .compression_type = compression_type2,
    .message_encoding = message_encoding2,
    .channel_type = channel_type2,
    .schema_name = schema_name1,
    .schema_encoding = schema_encoding1,
    .schema_definition = schema_desc1,
  };

  const LoggedChannelMetadata channel_metadata3{
    .channel_name = channel_name3,
    .compression_type = compression_type3,
    .message_encoding = message_encoding3,
    .channel_type = channel_type3,
    .schema_name = schema_name2,
    .schema_encoding = schema_encoding2,
    .schema_definition = schema_desc2,
  };

  const LoggedChannelMetadata channel_metadata4{
    .channel_name = channel_name4,
    .compression_type = compression_type4,
    .message_encoding = message_encoding4,
    .channel_type = channel_type4,
    .schema_name = schema_name2,
    .schema_encoding = schema_encoding2,
    .schema_definition = schema_desc2,
  };

  SECTION("Single channel with schema")
  {
    Writer<TestWriterPolicy> writer{
      memory_resource, memory_resource, max_write_mib_per_sec, max_log_file_duration, WriterEnvironment::normal};
    REQUIRE(writer.open_log(log_dir.string(), log_file_prefix, time1));
    REQUIRE(writer.add_channel(channel_metadata1, time1));
    schema_id_map[schema_name1] = 1U;
    channel_id_map[channel_name1] = 1U;
    REQUIRE(writer.get_pending_message_data_bytes() == 0U);
    REQUIRE(writer.close_log(time1));
    REQUIRE(writer.drain_async_operations());
    REQUIRE(writer.get_pending_message_data_bytes() == 0U);

    const auto maybe_file_data = tests::try_read_file((log_dir / "log_file_000000.olog").string());
    REQUIRE(maybe_file_data);
    const auto file_data = std::span{maybe_file_data->data(), maybe_file_data->size()};
    auto validate_result = tests::try_validate_log_header(file_data);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_schema_record(file_data, *validate_result, channel_metadata1, schema_id_map);
    REQUIRE(validate_result);
    validate_result =
      tests::try_validate_channel_record(file_data, *validate_result, channel_metadata1, schema_id_map, channel_id_map);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_pad_bytes(
      file_data,
      *validate_result,
      jewels::Aligner<TestWriterPolicy::alignment>::aligned_remainder(
        *validate_result + end_log_file_record_header_size + record_trailer_size));
    REQUIRE(validate_result);
    validate_result = tests::try_validate_end_log_file_record(
      file_data, *validate_result, false, LogTimestamp{0}, LogTimestamp{0}, LogTimestamp{0}, LogTimestamp{0});
    REQUIRE(validate_result == file_data.size());
  }

  SECTION("Channels with shared schemas")
  {
    Writer<TestWriterPolicy> writer{
      memory_resource, memory_resource, max_write_mib_per_sec, max_log_file_duration, WriterEnvironment::normal};
    REQUIRE(writer.open_log(log_dir.string(), log_file_prefix, time1));

    REQUIRE(writer.add_channel(channel_metadata1, time1));
    REQUIRE(writer.add_channel(channel_metadata1, time1));
    schema_id_map[schema_name1] = 1U;
    channel_id_map[channel_name1] = 1U;

    REQUIRE(writer.add_channel(channel_metadata2, time1));
    REQUIRE(writer.add_channel(channel_metadata2, time1));
    channel_id_map[channel_name2] = 2U;

    REQUIRE(writer.add_channel(channel_metadata3, time1));
    REQUIRE(writer.add_channel(channel_metadata3, time1));
    schema_id_map[schema_name2] = 2U;
    channel_id_map[channel_name3] = 3U;

    REQUIRE(writer.add_channel(channel_metadata4, time1));
    REQUIRE(writer.add_channel(channel_metadata4, time1));
    channel_id_map[channel_name4] = 4U;

    REQUIRE(writer.get_pending_message_data_bytes() == 0U);
    REQUIRE(writer.close_log(time1));
    REQUIRE(writer.drain_async_operations());
    REQUIRE(writer.get_pending_message_data_bytes() == 0U);

    const auto maybe_file_data = tests::try_read_file((log_dir / "log_file_000000.olog").string());
    REQUIRE(maybe_file_data);
    const auto file_data = std::span{maybe_file_data->data(), maybe_file_data->size()};
    auto validate_result = tests::try_validate_log_header(file_data);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_schema_record(file_data, *validate_result, channel_metadata1, schema_id_map);
    REQUIRE(validate_result);
    validate_result =
      tests::try_validate_channel_record(file_data, *validate_result, channel_metadata1, schema_id_map, channel_id_map);
    REQUIRE(validate_result);
    validate_result =
      tests::try_validate_channel_record(file_data, *validate_result, channel_metadata2, schema_id_map, channel_id_map);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_schema_record(file_data, *validate_result, channel_metadata3, schema_id_map);
    REQUIRE(validate_result);
    validate_result =
      tests::try_validate_channel_record(file_data, *validate_result, channel_metadata3, schema_id_map, channel_id_map);
    REQUIRE(validate_result);
    validate_result =
      tests::try_validate_channel_record(file_data, *validate_result, channel_metadata4, schema_id_map, channel_id_map);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_pad_bytes(
      file_data,
      *validate_result,
      jewels::Aligner<TestWriterPolicy::alignment>::aligned_remainder(
        *validate_result + end_log_file_record_header_size + record_trailer_size));
    REQUIRE(validate_result);
    validate_result = tests::try_validate_end_log_file_record(
      file_data, *validate_result, false, LogTimestamp{0}, LogTimestamp{0}, LogTimestamp{0}, LogTimestamp{0});
    REQUIRE(validate_result == file_data.size());
  }

  SECTION("Channels with shared schemas defined before open")
  {
    Writer<TestWriterPolicy> writer{
      memory_resource, memory_resource, max_write_mib_per_sec, max_log_file_duration, WriterEnvironment::normal};

    REQUIRE(writer.add_channel(channel_metadata1, time1));
    REQUIRE(writer.add_channel(channel_metadata1, time1));
    schema_id_map[schema_name1] = 1U;
    channel_id_map[channel_name1] = 1U;

    REQUIRE(writer.add_channel(channel_metadata2, time1));
    REQUIRE(writer.add_channel(channel_metadata2, time1));
    channel_id_map[channel_name2] = 2U;

    REQUIRE(writer.add_channel(channel_metadata3, time1));
    REQUIRE(writer.add_channel(channel_metadata3, time1));
    schema_id_map[schema_name2] = 2U;
    channel_id_map[channel_name3] = 3U;

    REQUIRE(writer.add_channel(channel_metadata4, time1));
    REQUIRE(writer.add_channel(channel_metadata4, time1));
    channel_id_map[channel_name4] = 4U;

    REQUIRE(writer.open_log(log_dir.string(), log_file_prefix, time1));
    REQUIRE(writer.get_pending_message_data_bytes() == 0U);
    REQUIRE(writer.close_log(time1));
    REQUIRE(writer.drain_async_operations());
    REQUIRE(writer.get_pending_message_data_bytes() == 0U);

    const auto maybe_file_data = tests::try_read_file((log_dir / "log_file_000000.olog").string());
    REQUIRE(maybe_file_data);
    const auto file_data = std::span{maybe_file_data->data(), maybe_file_data->size()};
    auto validate_result = tests::try_validate_log_header(file_data);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_schema_record(file_data, *validate_result, channel_metadata1, schema_id_map);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_schema_record(file_data, *validate_result, channel_metadata3, schema_id_map);
    REQUIRE(validate_result);
    validate_result =
      tests::try_validate_channel_record(file_data, *validate_result, channel_metadata1, schema_id_map, channel_id_map);
    REQUIRE(validate_result);
    validate_result =
      tests::try_validate_channel_record(file_data, *validate_result, channel_metadata2, schema_id_map, channel_id_map);
    REQUIRE(validate_result);
    validate_result =
      tests::try_validate_channel_record(file_data, *validate_result, channel_metadata3, schema_id_map, channel_id_map);
    REQUIRE(validate_result);
    validate_result =
      tests::try_validate_channel_record(file_data, *validate_result, channel_metadata4, schema_id_map, channel_id_map);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_pad_bytes(
      file_data,
      *validate_result,
      jewels::Aligner<TestWriterPolicy::alignment>::aligned_remainder(
        *validate_result + end_log_file_record_header_size + record_trailer_size));
    REQUIRE(validate_result);
    validate_result = tests::try_validate_end_log_file_record(
      file_data, *validate_result, false, LogTimestamp{0}, LogTimestamp{0}, LogTimestamp{0}, LogTimestamp{0});
    REQUIRE(validate_result == file_data.size());
  }

  SECTION("Schema name too long")
  {
    Writer<TestWriterPolicy> writer{
      memory_resource, memory_resource, max_write_mib_per_sec, max_log_file_duration, WriterEnvironment::normal};
    REQUIRE(writer.open_log(log_dir.string(), log_file_prefix, time1));

    const auto long_schema_name = std::string(64U * jewels::math::constants::bytes_per_kib<size_t>, 'X');
    REQUIRE(
      writer.add_channel(
        LoggedChannelMetadata{
          channel_name1,
          compression_type1,
          message_encoding1,
          channel_type1,
          long_schema_name,
          schema_encoding1,
          schema_desc1},
        time1) == jewels::unexpected(LogError::schema_name_exceeds_max_name_size));
    REQUIRE(writer.get_status().status_string == "Schema name exceeds max size (65535)");
  }

  SECTION("Channel name too long")
  {
    Writer<TestWriterPolicy> writer{
      memory_resource, memory_resource, max_write_mib_per_sec, max_log_file_duration, WriterEnvironment::normal};
    REQUIRE(writer.open_log(log_dir.string(), log_file_prefix, time1));

    const auto long_channel_name = std::string(64U * jewels::math::constants::bytes_per_kib<size_t>, 'X');
    REQUIRE(
      writer.add_channel(
        LoggedChannelMetadata{
          long_channel_name,
          compression_type1,
          message_encoding1,
          channel_type1,
          schema_name1,
          schema_encoding1,
          schema_desc1},
        time1) == jewels::unexpected(LogError::channel_name_exceeds_max_name_size));
    REQUIRE(writer.get_status().status_string == "Channel name exceeds max size (65535)");
  }

  SECTION("Schema definition string too long")
  {
    Writer<TestWriterPolicy> writer{
      memory_resource, memory_resource, max_write_mib_per_sec, max_log_file_duration, WriterEnvironment::normal};
    REQUIRE(writer.open_log(log_dir.string(), log_file_prefix, time1));

    const auto long_schema_definition = std::string((256U * jewels::math::constants::bytes_per_kib<size_t>)+1U, 'X');
    REQUIRE(
      writer.add_channel(
        LoggedChannelMetadata{
          channel_name1,
          compression_type1,
          message_encoding1,
          channel_type1,
          schema_name1,
          schema_encoding1,
          long_schema_definition},
        time1) == jewels::unexpected(LogError::schema_definition_exceeds_max_size));
    REQUIRE(writer.get_status().status_string == "Schema definition exceeds max size (262144)");
  }
}

TEST_CASE("Log messages")
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  static constexpr size_t max_write_mib_per_sec = 1U;
  static constexpr auto max_log_file_duration = std::chrono::seconds{0};
  static constexpr size_t message_buffer_count = 100U;
  const auto* log_file_prefix = "log_file_";

  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto log_dir = test_dir.get_path() / log_file_prefix;

  const auto* schema_name1 = "Schema 1";
  const auto schema_encoding1 = SchemaEncoding::unspecified;
  const auto* schema_desc1 = "Schema description 1";

  const auto* channel_name1 = "Channel 1";
  const auto compression_type1 = CompressionType::none;
  const auto message_encoding1 = MessageEncoding::unspecified;
  const auto channel_type1 = ChannelType::regular;

  jewels::SharedBufferPool<message_buffer_size, message_alignment> message_buffer_pool{
    memory_resource, message_buffer_count};

  const jewels::time::SteadyTime time1{std::chrono::seconds(1)};

  std::unordered_map<std::string_view, uint16_t> channel_id_map;
  std::unordered_map<std::string_view, uint16_t> schema_id_map;

  const LoggedChannelMetadata channel_metadata1{
    .channel_name = channel_name1,
    .compression_type = compression_type1,
    .message_encoding = message_encoding1,
    .channel_type = channel_type1,
    .schema_name = schema_name1,
    .schema_encoding = schema_encoding1,
    .schema_definition = schema_desc1,
  };

  schema_id_map[schema_name1] = 1U;
  channel_id_map[channel_name1] = 1U;

  SECTION("Single small message")
  {
    Writer<TestWriterPolicy> writer{
      memory_resource, memory_resource, max_write_mib_per_sec, max_log_file_duration, WriterEnvironment::normal};
    REQUIRE(writer.open_log(log_dir.string(), log_file_prefix, time1));
    REQUIRE(writer.add_channel(channel_metadata1, time1));
    REQUIRE(writer.get_pending_message_data_bytes() == 0U);

    const size_t header_size1 = 14U;
    const std::vector<std::byte> header1(header_size1, std::byte{'A'});
    const size_t message_size1 = 127U;
    auto message_result = message_buffer_pool.get_shared_buffer();
    REQUIRE(message_result);
    auto message_buffer1 = std::move(message_result).value();
    std::memset(message_buffer1->data(), 'B', message_size1);
    const std::span<std::byte> message1{message_buffer1->data(), message_size1};
    const uint32_t sequence_number1 = 1U;
    const LogTimestamp log_time1{std::chrono::nanoseconds(100)};
    const LogTimestamp message_time1{std::chrono::nanoseconds(1000)};

    const Message logged_message1{
      .channel_name = channel_name1,
      .sequence_number = sequence_number1,
      .log_time = log_time1,
      .message_time = message_time1,
      .header = {header1},
      .data = message1,
    };

    REQUIRE(writer.log_message(logged_message1, TestMessageHandleType{message_buffer1}, time1));
    REQUIRE(
      writer.get_pending_message_data_bytes() ==
      message_record_header_size + header_size1 + message_size1 + record_trailer_size);

    REQUIRE(writer.close_log(time1));
    REQUIRE(writer.drain_async_operations());
    REQUIRE(writer.get_pending_message_data_bytes() == 0U);

    SECTION("Continue writing into existing log")
    {
      const jewels::time::SteadyTime time2{std::chrono::seconds(2)};

      Writer<TestWriterPolicy> writer2{
        memory_resource, memory_resource, max_write_mib_per_sec, max_log_file_duration, WriterEnvironment::normal};
      REQUIRE(writer2.open_log(log_dir.string(), log_file_prefix, time2));
      REQUIRE(writer2.add_channel(channel_metadata1, time2));

      const size_t header_size2 = 14U;
      const std::vector<std::byte> header2(header_size2, std::byte{'C'});
      const size_t message_size2 = 127U;
      message_result = message_buffer_pool.get_shared_buffer();
      REQUIRE(message_result);
      auto message_buffer2 = std::move(message_result).value();
      std::memset(message_buffer2->data(), 'D', message_size2);
      const std::span<std::byte> message2{message_buffer2->data(), message_size2};
      const uint32_t sequence_number2 = 2U;
      const LogTimestamp log_time2{std::chrono::nanoseconds(200)};
      const LogTimestamp message_time2{std::chrono::nanoseconds(2000)};

      const Message logged_message2{
        .channel_name = channel_name1,
        .sequence_number = sequence_number2,
        .log_time = log_time2,
        .message_time = message_time2,
        .header = {header2},
        .data = message2,
      };

      REQUIRE(writer2.log_message(logged_message2, TestMessageHandleType{message_buffer2}, time2));
      REQUIRE(
        writer2.get_pending_message_data_bytes() ==
        message_record_header_size + header_size2 + message_size2 + record_trailer_size);

      REQUIRE(writer2.close_log(time2));
      REQUIRE(writer2.drain_async_operations());
      REQUIRE(writer2.get_pending_message_data_bytes() == 0U);

      const auto maybe_file_data = tests::try_read_file((log_dir / "log_file_000001.olog").string());
      REQUIRE(maybe_file_data);
      const auto file_data = std::span{maybe_file_data->data(), maybe_file_data->size()};
      auto validate_result = tests::try_validate_log_header(file_data);
      REQUIRE(validate_result);
      validate_result =
        tests::try_validate_schema_record(file_data, *validate_result, channel_metadata1, schema_id_map);
      REQUIRE(validate_result);
      validate_result = tests::try_validate_channel_record(
        file_data, *validate_result, channel_metadata1, schema_id_map, channel_id_map);
      REQUIRE(validate_result);
      validate_result =
        tests::try_validate_message_record(file_data, *validate_result, logged_message2, channel_id_map);
      REQUIRE(validate_result);
      validate_result = tests::try_validate_pad_bytes(
        file_data,
        *validate_result,
        jewels::Aligner<TestWriterPolicy::alignment>::aligned_remainder(
          *validate_result + end_log_file_record_header_size + record_trailer_size));
      REQUIRE(validate_result);
      validate_result = tests::try_validate_end_log_file_record(
        file_data, *validate_result, true, log_time2, log_time2, message_time2, message_time2);
      REQUIRE(validate_result == file_data.size());
    }

    const auto maybe_file_data = tests::try_read_file((log_dir / "log_file_000000.olog").string());
    REQUIRE(maybe_file_data);
    const auto file_data = std::span{maybe_file_data->data(), maybe_file_data->size()};
    auto validate_result = tests::try_validate_log_header(file_data);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_schema_record(file_data, *validate_result, channel_metadata1, schema_id_map);
    REQUIRE(validate_result);
    validate_result =
      tests::try_validate_channel_record(file_data, *validate_result, channel_metadata1, schema_id_map, channel_id_map);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_message_record(file_data, *validate_result, logged_message1, channel_id_map);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_pad_bytes(
      file_data,
      *validate_result,
      jewels::Aligner<TestWriterPolicy::alignment>::aligned_remainder(
        *validate_result + end_log_file_record_header_size + record_trailer_size));
    REQUIRE(validate_result);
    validate_result = tests::try_validate_end_log_file_record(
      file_data, *validate_result, true, log_time1, log_time1, message_time1, message_time1);
    REQUIRE(validate_result == file_data.size());
  }

  SECTION("Invalid message handle")
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
        schema_desc1},
      time1));

    const size_t header_size1 = 14U;
    const std::vector<std::byte> header1(header_size1, std::byte{'A'});
    const size_t message_size1 = message_buffer_size;
    auto message_result = message_buffer_pool.get_shared_buffer();
    REQUIRE(message_result);
    auto message_buffer1 = std::move(message_result).value();
    auto message_handle1 = TestMessageHandleType{message_buffer1};
    std::memset(message_buffer1->data(), 'B', message_size1);
    const uint32_t sequence_number1 = 1U;
    const LogTimestamp log_time1{std::chrono::nanoseconds(100)};
    const LogTimestamp message_time1{std::chrono::nanoseconds(1000)};

    const Message logged_message1{
      .channel_name = channel_name1,
      .sequence_number = sequence_number1,
      .log_time = log_time1,
      .message_time = message_time1,
      .header = {header1},
      .data = *message_buffer1,
    };

    message_handle1.set_is_valid(false);
    REQUIRE(
      writer.log_message(logged_message1, message_handle1, time1) == jewels::unexpected(LogError::message_dropped));
    REQUIRE(writer.get_pending_message_data_bytes() == 0U);
  }

  SECTION("Aligned zero copy message")
  {
    Writer<TestWriterPolicy> writer{
      memory_resource, memory_resource, max_write_mib_per_sec, max_log_file_duration, WriterEnvironment::normal};
    REQUIRE(writer.open_log(log_dir.string(), log_file_prefix, time1));
    REQUIRE(writer.add_channel(channel_metadata1, time1));

    const size_t header_size1 = 14U;
    const std::vector<std::byte> header1(header_size1, std::byte{'A'});
    const size_t message_size1 = 4U * jewels::math::constants::bytes_per_kib<size_t>;
    auto message_result = message_buffer_pool.get_shared_buffer();
    REQUIRE(message_result);
    auto message_buffer1 = std::move(message_result).value();
    std::memset(message_buffer1->data(), 'B', message_size1);
    const auto message_data1 = std::span{*message_buffer1}.first(message_size1);
    const uint32_t sequence_number1 = 1U;
    const LogTimestamp log_time1{std::chrono::nanoseconds(100)};
    const LogTimestamp message_time1{std::chrono::nanoseconds(1000)};

    const Message logged_message1{
      .channel_name = channel_name1,
      .sequence_number = sequence_number1,
      .log_time = log_time1,
      .message_time = message_time1,
      .header = {header1},
      .data = message_data1,
    };

    REQUIRE(writer.log_message(logged_message1, TestMessageHandleType{message_buffer1}, time1));
    REQUIRE(
      writer.get_pending_message_data_bytes() ==
      message_record_header_size + header_size1 + message_size1 + record_trailer_size);
    REQUIRE(message_buffer1.get_reference_count() == 2U);

    REQUIRE(writer.close_log(time1));
    REQUIRE(writer.drain_async_operations());
    REQUIRE(writer.get_pending_message_data_bytes() == 0U);
    REQUIRE(message_buffer1.get_reference_count() == 1U);

    const auto maybe_file_data = tests::try_read_file((log_dir / "log_file_000000.olog").string());
    REQUIRE(maybe_file_data);
    const auto file_data = std::span{maybe_file_data->data(), maybe_file_data->size()};
    auto validate_result = tests::try_validate_log_header(file_data);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_schema_record(file_data, *validate_result, channel_metadata1, schema_id_map);
    REQUIRE(validate_result);
    validate_result =
      tests::try_validate_channel_record(file_data, *validate_result, channel_metadata1, schema_id_map, channel_id_map);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_pad_bytes(
      file_data,
      *validate_result,
      jewels::Aligner<TestWriterPolicy::alignment>::aligned_remainder(
        *validate_result + message_record_header_size + header1.size()));
    REQUIRE(validate_result);
    validate_result = tests::try_validate_message_record(file_data, *validate_result, logged_message1, channel_id_map);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_pad_bytes(
      file_data,
      *validate_result,
      jewels::Aligner<TestWriterPolicy::alignment>::aligned_remainder(
        *validate_result + end_log_file_record_header_size + record_trailer_size));
    REQUIRE(validate_result);
    validate_result = tests::try_validate_end_log_file_record(
      file_data, *validate_result, true, log_time1, log_time1, message_time1, message_time1);
    REQUIRE(validate_result == file_data.size());
  }

  SECTION("Misaligned zero copy message")
  {
    Writer<TestWriterPolicy> writer{
      memory_resource, memory_resource, max_write_mib_per_sec, max_log_file_duration, WriterEnvironment::normal};
    REQUIRE(writer.open_log(log_dir.string(), log_file_prefix, time1));
    REQUIRE(writer.add_channel(channel_metadata1, time1));

    const size_t header_size1 = 14U;
    const std::vector<std::byte> header1(header_size1, std::byte{'A'});
    const size_t message_size1 = (4U * jewels::math::constants::bytes_per_kib<size_t>)+2U;
    auto message_result = message_buffer_pool.get_shared_buffer();
    REQUIRE(message_result);
    auto message_buffer1 = std::move(message_result).value();
    std::memset(&message_buffer1->at((4U * jewels::math::constants::bytes_per_kib<size_t>)-1U), 'B', message_size1);
    const auto message_data1 =
      std::span{*message_buffer1}.subspan((4U * jewels::math::constants::bytes_per_kib<size_t>)-1U, message_size1);
    const uint32_t sequence_number1 = 1U;
    const LogTimestamp log_time1{std::chrono::nanoseconds(100)};
    const LogTimestamp message_time1{std::chrono::nanoseconds(1000)};

    const Message logged_message1{
      .channel_name = channel_name1,
      .sequence_number = sequence_number1,
      .log_time = log_time1,
      .message_time = message_time1,
      .header = {header1},
      .data = message_data1,
    };

    REQUIRE(writer.log_message(logged_message1, TestMessageHandleType{message_buffer1}, time1));
    REQUIRE(
      writer.get_pending_message_data_bytes() ==
      message_record_header_size + header_size1 + message_size1 + record_trailer_size);
    REQUIRE(message_buffer1.get_reference_count() == 2U);

    REQUIRE(writer.close_log(time1));
    REQUIRE(writer.drain_async_operations());
    REQUIRE(writer.get_pending_message_data_bytes() == 0U);

    const auto maybe_file_data = tests::try_read_file((log_dir / "log_file_000000.olog").string());
    REQUIRE(maybe_file_data);
    const auto file_data = std::span{maybe_file_data->data(), maybe_file_data->size()};
    auto validate_result = tests::try_validate_log_header(file_data);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_schema_record(file_data, *validate_result, channel_metadata1, schema_id_map);
    REQUIRE(validate_result);
    validate_result =
      tests::try_validate_channel_record(file_data, *validate_result, channel_metadata1, schema_id_map, channel_id_map);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_pad_bytes(
      file_data,
      *validate_result,
      jewels::Aligner<TestWriterPolicy::alignment>::aligned_remainder(
        *validate_result + message_record_header_size + header1.size() + 1U));
    REQUIRE(validate_result);
    validate_result = tests::try_validate_message_record(file_data, *validate_result, logged_message1, channel_id_map);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_pad_bytes(
      file_data,
      *validate_result,
      jewels::Aligner<TestWriterPolicy::alignment>::aligned_remainder(
        *validate_result + end_log_file_record_header_size + record_trailer_size));
    REQUIRE(validate_result);
    validate_result = tests::try_validate_end_log_file_record(
      file_data, *validate_result, true, log_time1, log_time1, message_time1, message_time1);
    REQUIRE(validate_result == file_data.size());
  }

  SECTION("Badly misaligned zero copy message")
  {
    Writer<TestWriterPolicy> writer{
      memory_resource, memory_resource, max_write_mib_per_sec, max_log_file_duration, WriterEnvironment::normal};
    REQUIRE(writer.open_log(log_dir.string(), log_file_prefix, time1));
    REQUIRE(writer.add_channel(channel_metadata1, time1));

    const size_t header_size1 = 14U;
    const std::vector<std::byte> header1(header_size1, std::byte{'A'});
    const size_t message_size1 = 4U * jewels::math::constants::bytes_per_kib<size_t>;
    auto message_result = message_buffer_pool.get_shared_buffer();
    REQUIRE(message_result);
    auto message_buffer1 = std::move(message_result).value();
    std::memset(&message_buffer1->at(1U), 'B', message_size1);
    const auto message_data1 = std::span{*message_buffer1}.subspan(1U, message_size1);
    const uint32_t sequence_number1 = 1U;
    const LogTimestamp log_time1{std::chrono::nanoseconds(100)};
    const LogTimestamp message_time1{std::chrono::nanoseconds(1000)};

    const Message logged_message1{
      .channel_name = channel_name1,
      .sequence_number = sequence_number1,
      .log_time = log_time1,
      .message_time = message_time1,
      .header = {header1},
      .data = message_data1,
    };

    REQUIRE(writer.log_message(logged_message1, TestMessageHandleType{message_buffer1}, time1));
    REQUIRE(
      writer.get_pending_message_data_bytes() ==
      message_record_header_size + header_size1 + message_size1 + record_trailer_size);
    REQUIRE(message_buffer1.get_reference_count() == 1U);

    REQUIRE(writer.close_log(time1));
    REQUIRE(writer.drain_async_operations());
    REQUIRE(writer.get_pending_message_data_bytes() == 0U);

    const auto maybe_file_data = tests::try_read_file((log_dir / "log_file_000000.olog").string());
    REQUIRE(maybe_file_data);
    const auto file_data = std::span{maybe_file_data->data(), maybe_file_data->size()};
    auto validate_result = tests::try_validate_log_header(file_data);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_schema_record(file_data, *validate_result, channel_metadata1, schema_id_map);
    REQUIRE(validate_result);
    validate_result =
      tests::try_validate_channel_record(file_data, *validate_result, channel_metadata1, schema_id_map, channel_id_map);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_pad_bytes(
      file_data,
      *validate_result,
      jewels::Aligner<TestWriterPolicy::alignment>::aligned_remainder(
        *validate_result + message_record_header_size + header1.size() - 1U));
    REQUIRE(validate_result);
    validate_result = tests::try_validate_message_record(file_data, *validate_result, logged_message1, channel_id_map);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_pad_bytes(
      file_data,
      *validate_result,
      jewels::Aligner<TestWriterPolicy::alignment>::aligned_remainder(
        *validate_result + end_log_file_record_header_size + record_trailer_size));
    REQUIRE(validate_result);
    validate_result = tests::try_validate_end_log_file_record(
      file_data, *validate_result, true, log_time1, log_time1, message_time1, message_time1);
    REQUIRE(validate_result == file_data.size());
  }

  SECTION("Write backlog exceeded")
  {
    Writer<TestWriterPolicy> writer{
      memory_resource, memory_resource, max_write_mib_per_sec, max_log_file_duration, WriterEnvironment::normal};
    REQUIRE(writer.open_log(log_dir.string(), log_file_prefix, time1));
    REQUIRE(writer.add_channel(channel_metadata1, time1));

    const size_t header_size1 = 14U;
    const std::vector<std::byte> header1(header_size1, std::byte{'A'});
    const size_t message_size1 = 127U;
    auto message_result = message_buffer_pool.get_shared_buffer();
    REQUIRE(message_result);
    auto message_buffer1 = std::move(message_result).value();
    std::memset(message_buffer1->data(), 'B', message_size1);
    const std::span<std::byte> message1{message_buffer1->data(), message_size1};
    const LogTimestamp log_time1{std::chrono::nanoseconds(100)};
    const LogTimestamp log_time2{std::chrono::nanoseconds(200)};
    const LogTimestamp log_time3{std::chrono::nanoseconds(300)};
    const LogTimestamp message_time1{std::chrono::nanoseconds(1000)};
    const LogTimestamp message_time2{std::chrono::nanoseconds(2000)};
    const LogTimestamp message_time3{std::chrono::nanoseconds(3000)};

    const jewels::time::SteadyTime time2 = time1 + TestWriterPolicy::max_write_backlog;
    const uint32_t sequence_number1 = 1U;

    const Message logged_message1{
      .channel_name = channel_name1,
      .sequence_number = sequence_number1,
      .log_time = log_time1,
      .message_time = message_time1,
      .header = {header1},
      .data = message1,
    };

    REQUIRE(writer.log_message(logged_message1, TestMessageHandleType{message_buffer1}, time2));
    const auto expected_message_data_bytes =
      message_record_header_size + header_size1 + message_size1 + record_trailer_size;
    REQUIRE(writer.get_pending_message_data_bytes() == expected_message_data_bytes);

    const jewels::time::SteadyTime time3 = time2 + std::chrono::nanoseconds(1);
    const uint32_t sequence_number2 = 2U;

    const Message logged_message2{
      .channel_name = channel_name1,
      .sequence_number = sequence_number2,
      .log_time = log_time2,
      .message_time = message_time2,
      .header = {header1},
      .data = message1,
    };

    REQUIRE(
      writer.log_message(logged_message2, TestMessageHandleType{message_buffer1}, time3) ==
      jewels::unexpected(LogError::message_dropped));
    REQUIRE(writer.get_pending_message_data_bytes() == expected_message_data_bytes);
    REQUIRE(writer.get_state() == WriterState::logging);
    REQUIRE(writer.get_and_reset_drop_count() == 1U);

    const jewels::time::SteadyTime time4 = time3 + std::chrono::seconds(1);
    writer.periodic_callback(time4);
    REQUIRE(writer.drain_async_operations());
    REQUIRE(writer.get_pending_message_data_bytes() == 0U);

    const uint32_t sequence_number3 = 3U;

    const Message logged_message3{
      .channel_name = channel_name1,
      .sequence_number = sequence_number3,
      .log_time = log_time3,
      .message_time = message_time3,
      .header = {header1},
      .data = message1,
    };

    REQUIRE(writer.log_message(logged_message3, TestMessageHandleType{message_buffer1}, time4));
    REQUIRE(writer.get_pending_message_data_bytes() == expected_message_data_bytes);

    REQUIRE(writer.close_log(time4));
    REQUIRE(writer.drain_async_operations());
    REQUIRE(writer.get_pending_message_data_bytes() == 0U);

    const auto maybe_file_data = tests::try_read_file((log_dir / "log_file_000000.olog").string());
    REQUIRE(maybe_file_data);
    const auto file_data = std::span{maybe_file_data->data(), maybe_file_data->size()};
    auto validate_result = tests::try_validate_log_header(file_data);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_schema_record(file_data, *validate_result, channel_metadata1, schema_id_map);
    REQUIRE(validate_result);
    validate_result =
      tests::try_validate_channel_record(file_data, *validate_result, channel_metadata1, schema_id_map, channel_id_map);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_message_record(file_data, *validate_result, logged_message1, channel_id_map);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_pad_bytes(
      file_data, *validate_result, jewels::Aligner<TestWriterPolicy::alignment>::aligned_remainder(*validate_result));
    REQUIRE(validate_result);
    validate_result = tests::try_validate_message_record(file_data, *validate_result, logged_message3, channel_id_map);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_pad_bytes(
      file_data,
      *validate_result,
      jewels::Aligner<TestWriterPolicy::alignment>::aligned_remainder(
        *validate_result + end_log_file_record_header_size + record_trailer_size));
    REQUIRE(validate_result);
    validate_result = tests::try_validate_end_log_file_record(
      file_data, *validate_result, true, log_time1, log_time3, message_time1, message_time3);
    REQUIRE(validate_result == file_data.size());
  }

  SECTION("Write rate exceeded")
  {
    Writer<TestWriterPolicy> writer{
      memory_resource, memory_resource, max_write_mib_per_sec, max_log_file_duration, WriterEnvironment::normal};
    REQUIRE(writer.open_log(log_dir.string(), log_file_prefix, time1));
    REQUIRE(writer.add_channel(channel_metadata1, time1));

    const size_t header_size1 = 14U;
    const std::vector<std::byte> header1(header_size1, std::byte{'A'});
    const size_t message_size1 = 127U;
    auto message_result = message_buffer_pool.get_shared_buffer();
    REQUIRE(message_result);
    auto message_buffer1 = std::move(message_result).value();
    std::memset(message_buffer1->data(), 'B', message_size1);
    const std::span<std::byte> message1{message_buffer1->data(), message_size1};
    const LogTimestamp log_time1{std::chrono::nanoseconds(100)};
    const LogTimestamp log_time2{std::chrono::nanoseconds(200)};
    const LogTimestamp log_time3{std::chrono::nanoseconds(300)};
    const LogTimestamp message_time1{std::chrono::nanoseconds(1000)};
    const LogTimestamp message_time2{std::chrono::nanoseconds(2000)};
    const LogTimestamp message_time3{std::chrono::nanoseconds(3000)};

    const jewels::time::SteadyTime time2 = time1 + TestWriterPolicy::max_write_backlog;
    const uint32_t sequence_number1 = 1U;

    const Message logged_message1{
      .channel_name = channel_name1,
      .sequence_number = sequence_number1,
      .log_time = log_time1,
      .message_time = message_time1,
      .header = {header1},
      .data = message1,
    };

    size_t expected_message_data_bytes = 0U;
    size_t message_count = 0U;

    while (expected_message_data_bytes < max_write_mib_per_sec * jewels::math::constants::bytes_per_mib<size_t>)
    {
      REQUIRE(writer.log_message(logged_message1, TestMessageHandleType{message_buffer1}, time2));
      expected_message_data_bytes += message_record_header_size + header_size1 + message_size1 + record_trailer_size;
      REQUIRE(writer.get_pending_message_data_bytes() == expected_message_data_bytes);
      ++message_count;
    }

    const jewels::time::SteadyTime time3 = time2 + std::chrono::nanoseconds(1);
    const uint32_t sequence_number2 = 2U;

    const Message logged_message2{
      .channel_name = channel_name1,
      .sequence_number = sequence_number2,
      .log_time = log_time2,
      .message_time = message_time2,
      .header = {header1},
      .data = message1,
    };

    REQUIRE(
      writer.log_message(logged_message2, TestMessageHandleType{message_buffer1}, time3) ==
      jewels::unexpected(LogError::message_dropped));
    REQUIRE(writer.get_pending_message_data_bytes() == expected_message_data_bytes);
    REQUIRE(writer.get_state() == WriterState::logging);
    REQUIRE(writer.get_and_reset_drop_count() == 1U);

    const jewels::time::SteadyTime time4 = time3 + std::chrono::seconds(1);
    writer.periodic_callback(time4);
    REQUIRE(writer.drain_async_operations());
    REQUIRE(writer.get_pending_message_data_bytes() == 0U);

    const uint32_t sequence_number3 = 3U;

    const Message logged_message3{
      .channel_name = channel_name1,
      .sequence_number = sequence_number3,
      .log_time = log_time3,
      .message_time = message_time3,
      .header = {header1},
      .data = message1,
    };

    REQUIRE(writer.log_message(logged_message3, TestMessageHandleType{message_buffer1}, time4));
    REQUIRE(
      writer.get_pending_message_data_bytes() ==
      message_record_header_size + header_size1 + message_size1 + record_trailer_size);

    REQUIRE(writer.close_log(time4));
    REQUIRE(writer.drain_async_operations());
    REQUIRE(writer.get_pending_message_data_bytes() == 0U);

    const auto maybe_file_data = tests::try_read_file((log_dir / "log_file_000000.olog").string());
    REQUIRE(maybe_file_data);
    const auto file_data = std::span{maybe_file_data->data(), maybe_file_data->size()};
    auto validate_result = tests::try_validate_log_header(file_data);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_schema_record(file_data, *validate_result, channel_metadata1, schema_id_map);
    REQUIRE(validate_result);
    validate_result =
      tests::try_validate_channel_record(file_data, *validate_result, channel_metadata1, schema_id_map, channel_id_map);
    REQUIRE(validate_result);
    for (size_t i = 0U; i < message_count; ++i)
    {
      validate_result =
        tests::try_validate_message_record(file_data, *validate_result, logged_message1, channel_id_map);
      REQUIRE(validate_result);
    }
    validate_result = tests::try_validate_pad_bytes(
      file_data, *validate_result, jewels::Aligner<TestWriterPolicy::alignment>::aligned_remainder(*validate_result));
    REQUIRE(validate_result);
    validate_result = tests::try_validate_message_record(file_data, *validate_result, logged_message3, channel_id_map);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_pad_bytes(
      file_data,
      *validate_result,
      jewels::Aligner<TestWriterPolicy::alignment>::aligned_remainder(
        *validate_result + end_log_file_record_header_size + record_trailer_size));
    REQUIRE(validate_result);
    validate_result = tests::try_validate_end_log_file_record(
      file_data, *validate_result, true, log_time1, log_time3, message_time1, message_time3);
    REQUIRE(validate_result == file_data.size());
  }

  SECTION("Split files when max size is reached")
  {
    Writer<TestWriterPolicy> writer{
      memory_resource, memory_resource, max_write_mib_per_sec * 4U, max_log_file_duration, WriterEnvironment::normal};
    REQUIRE(writer.open_log(log_dir.string(), log_file_prefix, time1));
    REQUIRE(writer.add_channel(channel_metadata1, time1));

    auto message_result = message_buffer_pool.get_shared_buffer();
    REQUIRE(message_result);
    auto message_buffer = std::move(message_result).value();
    auto message_handle = TestMessageHandleType{message_buffer};
    std::memset(message_buffer->data(), 'B', message_buffer->size());
    const auto message_data = std::span{*message_buffer};

    constexpr LogTimestamp log_time1{std::chrono::nanoseconds(100)};
    constexpr LogTimestamp message_time1{std::chrono::nanoseconds(1000)};

    std::vector<Message> logged_messages;

    const uint32_t messages_per_file = 128U;
    for (uint32_t sequence_number = 1U; sequence_number <= messages_per_file; ++sequence_number)
    {
      logged_messages.emplace_back(
        Message{
          .channel_name = channel_name1,
          .sequence_number = sequence_number,
          .log_time = log_time1,
          .message_time = message_time1,
          .header = {},
          .data = message_data,
        });
      REQUIRE(writer.log_message_wait(logged_messages.back(), message_handle, time1));
    }

    // Next write splits to a new log file
    const Message logged_message2{
      .channel_name = channel_name1,
      .sequence_number = messages_per_file,
      .log_time = log_time1,
      .message_time = message_time1,
      .header = {},
      .data = message_data,
    };

    REQUIRE(writer.log_message(logged_message2, message_handle, time1));

    REQUIRE(writer.close_log(time1));
    REQUIRE(writer.drain_async_operations());
    REQUIRE(writer.get_pending_message_data_bytes() == 0U);

    const auto maybe_file1_data = tests::try_read_file((log_dir / "log_file_000000.olog").string());
    REQUIRE(maybe_file1_data);
    const auto file1_data = std::span{maybe_file1_data->data(), maybe_file1_data->size()};
    auto validate_result = tests::try_validate_log_header(file1_data);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_schema_record(file1_data, *validate_result, channel_metadata1, schema_id_map);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_channel_record(
      file1_data, *validate_result, channel_metadata1, schema_id_map, channel_id_map);
    REQUIRE(validate_result);
    for (const auto& logged_message : logged_messages)
    {
      validate_result = tests::try_validate_pad_bytes(
        file1_data,
        *validate_result,
        jewels::Aligner<TestWriterPolicy::alignment>::aligned_remainder(*validate_result + message_record_header_size));
      validate_result =
        tests::try_validate_message_record(file1_data, *validate_result, logged_message, channel_id_map);
      REQUIRE(validate_result);
    }
    validate_result = tests::try_validate_pad_bytes(
      file1_data,
      *validate_result,
      jewels::Aligner<TestWriterPolicy::alignment>::aligned_remainder(
        *validate_result + end_log_file_record_header_size + record_trailer_size));
    REQUIRE(validate_result);
    validate_result = tests::try_validate_end_log_file_record(
      file1_data, *validate_result, true, log_time1, log_time1, message_time1, message_time1);
    REQUIRE(validate_result == file1_data.size());

    const auto maybe_file2_data = tests::try_read_file((log_dir / "log_file_000001.olog").string());
    REQUIRE(maybe_file2_data);
    const auto file2_data = std::span{maybe_file2_data->data(), maybe_file2_data->size()};
    validate_result = tests::try_validate_log_header(file2_data);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_schema_record(file2_data, *validate_result, channel_metadata1, schema_id_map);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_channel_record(
      file2_data, *validate_result, channel_metadata1, schema_id_map, channel_id_map);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_pad_bytes(
      file2_data,
      *validate_result,
      jewels::Aligner<TestWriterPolicy::alignment>::aligned_remainder(*validate_result + message_record_header_size));
    REQUIRE(validate_result);
    validate_result = tests::try_validate_message_record(file2_data, *validate_result, logged_message2, channel_id_map);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_pad_bytes(
      file2_data,
      *validate_result,
      jewels::Aligner<TestWriterPolicy::alignment>::aligned_remainder(
        *validate_result + end_log_file_record_header_size + record_trailer_size));
    REQUIRE(validate_result);
    validate_result = tests::try_validate_end_log_file_record(
      file2_data, *validate_result, true, log_time1, log_time1, message_time1, message_time1);
    REQUIRE(validate_result == file2_data.size());
  }

  SECTION("Split files when max duration is reached")
  {
    constexpr auto short_max_log_file_duration = std::chrono::seconds(2);
    Writer<TestWriterPolicy> writer{
      memory_resource, memory_resource, max_write_mib_per_sec, short_max_log_file_duration, WriterEnvironment::normal};
    REQUIRE(writer.open_log(log_dir.string(), log_file_prefix, time1));
    REQUIRE(writer.add_channel(channel_metadata1, time1));

    auto message_result = message_buffer_pool.get_shared_buffer();
    REQUIRE(message_result);
    auto message_buffer = std::move(message_result).value();
    auto message_handle = TestMessageHandleType{message_buffer};
    std::memset(message_buffer->data(), 'B', message_buffer->size());
    const auto message_data = std::span{*message_buffer};

    constexpr LogTimestamp log_time1{std::chrono::nanoseconds(100)};
    constexpr LogTimestamp log_time2{std::chrono::seconds(3)};
    constexpr LogTimestamp message_time1{std::chrono::nanoseconds(1000)};

    const Message logged_message1{
      .channel_name = channel_name1,
      .sequence_number = 1,
      .log_time = log_time1,
      .message_time = message_time1,
      .header = {},
      .data = message_data,
    };

    REQUIRE(writer.log_message_wait(logged_message1, message_handle, time1));

    const Message logged_message2{
      .channel_name = channel_name1,
      .sequence_number = 2,
      .log_time = log_time2,
      .message_time = message_time1,
      .header = {},
      .data = message_data,
    };

    REQUIRE(writer.log_message_wait(logged_message2, message_handle, time1));

    const Message logged_message3{
      .channel_name = channel_name1,
      .sequence_number = 3,
      .log_time = log_time2,
      .message_time = message_time1,
      .header = {},
      .data = message_data,
    };

    // Next write splits to a new log file
    REQUIRE(writer.log_message(logged_message3, message_handle, time1));

    REQUIRE(writer.close_log(time1));
    REQUIRE(writer.drain_async_operations());
    REQUIRE(writer.get_pending_message_data_bytes() == 0U);

    const auto maybe_file1_data = tests::try_read_file((log_dir / "log_file_000000.olog").string());
    REQUIRE(maybe_file1_data);
    const auto file1_data = std::span{maybe_file1_data->data(), maybe_file1_data->size()};
    auto validate_result = tests::try_validate_log_header(file1_data);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_schema_record(file1_data, *validate_result, channel_metadata1, schema_id_map);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_channel_record(
      file1_data, *validate_result, channel_metadata1, schema_id_map, channel_id_map);
    REQUIRE(validate_result);

    validate_result = tests::try_validate_pad_bytes(
      file1_data,
      *validate_result,
      jewels::Aligner<TestWriterPolicy::alignment>::aligned_remainder(*validate_result + message_record_header_size));
    validate_result = tests::try_validate_message_record(file1_data, *validate_result, logged_message1, channel_id_map);
    REQUIRE(validate_result);

    validate_result = tests::try_validate_pad_bytes(
      file1_data,
      *validate_result,
      jewels::Aligner<TestWriterPolicy::alignment>::aligned_remainder(*validate_result + message_record_header_size));
    validate_result = tests::try_validate_message_record(file1_data, *validate_result, logged_message2, channel_id_map);
    REQUIRE(validate_result);

    validate_result = tests::try_validate_pad_bytes(
      file1_data,
      *validate_result,
      jewels::Aligner<TestWriterPolicy::alignment>::aligned_remainder(
        *validate_result + end_log_file_record_header_size + record_trailer_size));
    REQUIRE(validate_result);
    validate_result = tests::try_validate_end_log_file_record(
      file1_data, *validate_result, true, log_time1, log_time2, message_time1, message_time1);
    REQUIRE(validate_result == file1_data.size());

    const auto maybe_file2_data = tests::try_read_file((log_dir / "log_file_000001.olog").string());
    REQUIRE(maybe_file2_data);
    const auto file2_data = std::span{maybe_file2_data->data(), maybe_file2_data->size()};
    validate_result = tests::try_validate_log_header(file2_data);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_schema_record(file2_data, *validate_result, channel_metadata1, schema_id_map);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_channel_record(
      file2_data, *validate_result, channel_metadata1, schema_id_map, channel_id_map);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_pad_bytes(
      file2_data,
      *validate_result,
      jewels::Aligner<TestWriterPolicy::alignment>::aligned_remainder(*validate_result + message_record_header_size));
    REQUIRE(validate_result);
    validate_result = tests::try_validate_message_record(file2_data, *validate_result, logged_message3, channel_id_map);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_pad_bytes(
      file2_data,
      *validate_result,
      jewels::Aligner<TestWriterPolicy::alignment>::aligned_remainder(
        *validate_result + end_log_file_record_header_size + record_trailer_size));
    REQUIRE(validate_result);
    validate_result = tests::try_validate_end_log_file_record(
      file2_data, *validate_result, true, log_time2, log_time2, message_time1, message_time1);
    REQUIRE(validate_result == file2_data.size());
  }

  SECTION("Don't split files when max duration is zero")
  {
    constexpr auto default_max_log_file_duration_s = std::chrono::seconds(0);
    Writer<TestWriterPolicy> writer{
      memory_resource,
      memory_resource,
      max_write_mib_per_sec,
      default_max_log_file_duration_s,
      WriterEnvironment::normal};
    REQUIRE(writer.open_log(log_dir.string(), log_file_prefix, time1));
    REQUIRE(writer.add_channel(channel_metadata1, time1));

    auto message_result = message_buffer_pool.get_shared_buffer();
    REQUIRE(message_result);
    auto message_buffer = std::move(message_result).value();
    auto message_handle = TestMessageHandleType{message_buffer};
    std::memset(message_buffer->data(), 'B', message_buffer->size());
    const auto message_data = std::span{*message_buffer};

    constexpr LogTimestamp log_time1{std::chrono::nanoseconds(100)};
    constexpr LogTimestamp log_time2{std::chrono::seconds(3)};
    constexpr LogTimestamp message_time1{std::chrono::nanoseconds(1000)};

    const Message logged_message1{
      .channel_name = channel_name1,
      .sequence_number = 1,
      .log_time = log_time1,
      .message_time = message_time1,
      .header = {},
      .data = message_data,
    };

    REQUIRE(writer.log_message_wait(logged_message1, message_handle, time1));

    const Message logged_message2{
      .channel_name = channel_name1,
      .sequence_number = 2,
      .log_time = log_time2,
      .message_time = message_time1,
      .header = {},
      .data = message_data,
    };

    REQUIRE(writer.log_message_wait(logged_message2, message_handle, time1));

    // Next write splits to a new log file
    const Message logged_message3{
      .channel_name = channel_name1,
      .sequence_number = 3,
      .log_time = log_time2,
      .message_time = message_time1,
      .header = {},
      .data = message_data,
    };

    REQUIRE(writer.log_message(logged_message3, message_handle, time1));

    REQUIRE(writer.close_log(time1));
    REQUIRE(writer.drain_async_operations());
    REQUIRE(writer.get_pending_message_data_bytes() == 0U);

    const auto maybe_file1_data = tests::try_read_file((log_dir / "log_file_000000.olog").string());
    REQUIRE(maybe_file1_data);
    const auto file1_data = std::span{maybe_file1_data->data(), maybe_file1_data->size()};
    auto validate_result = tests::try_validate_log_header(file1_data);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_schema_record(file1_data, *validate_result, channel_metadata1, schema_id_map);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_channel_record(
      file1_data, *validate_result, channel_metadata1, schema_id_map, channel_id_map);
    REQUIRE(validate_result);

    validate_result = tests::try_validate_pad_bytes(
      file1_data,
      *validate_result,
      jewels::Aligner<TestWriterPolicy::alignment>::aligned_remainder(*validate_result + message_record_header_size));
    validate_result = tests::try_validate_message_record(file1_data, *validate_result, logged_message1, channel_id_map);
    REQUIRE(validate_result);

    validate_result = tests::try_validate_pad_bytes(
      file1_data,
      *validate_result,
      jewels::Aligner<TestWriterPolicy::alignment>::aligned_remainder(*validate_result + message_record_header_size));
    validate_result = tests::try_validate_message_record(file1_data, *validate_result, logged_message2, channel_id_map);
    REQUIRE(validate_result);

    validate_result = tests::try_validate_pad_bytes(
      file1_data,
      *validate_result,
      jewels::Aligner<TestWriterPolicy::alignment>::aligned_remainder(*validate_result + message_record_header_size));
    validate_result = tests::try_validate_message_record(file1_data, *validate_result, logged_message3, channel_id_map);
    REQUIRE(validate_result);

    validate_result = tests::try_validate_pad_bytes(
      file1_data,
      *validate_result,
      jewels::Aligner<TestWriterPolicy::alignment>::aligned_remainder(
        *validate_result + end_log_file_record_header_size + record_trailer_size));
    REQUIRE(validate_result);
    validate_result = tests::try_validate_end_log_file_record(
      file1_data, *validate_result, true, log_time1, log_time2, message_time1, message_time1);
    REQUIRE(validate_result == file1_data.size());
  }

  SECTION("Pause/Resume")
  {
    Writer<TestWriterPolicy> writer{
      memory_resource, memory_resource, max_write_mib_per_sec, max_log_file_duration, WriterEnvironment::normal};
    REQUIRE(writer.open_log_paused(log_dir.string(), log_file_prefix));
    REQUIRE(writer.get_state() == WriterState::paused);
    REQUIRE(writer.add_channel(channel_metadata1, time1));
    REQUIRE(writer.resume_logging(time1));
    REQUIRE(writer.get_state() == WriterState::logging);

    auto message_result = message_buffer_pool.get_shared_buffer();
    REQUIRE(message_result);
    auto message_buffer = std::move(message_result).value();
    auto message_handle = TestMessageHandleType{message_buffer};
    std::memset(message_buffer->data(), 'B', message_buffer->size());
    const auto message_data = std::span{*message_buffer};

    const LogTimestamp log_time1{std::chrono::nanoseconds(100)};
    const LogTimestamp message_time1{std::chrono::nanoseconds(1000)};

    std::vector<Message> logged_messages1;

    const uint32_t messages_per_file = 5U;
    for (uint32_t sequence_number = 1U; sequence_number <= messages_per_file; ++sequence_number)
    {
      logged_messages1.emplace_back(
        Message{
          .channel_name = channel_name1,
          .sequence_number = sequence_number,
          .log_time = log_time1,
          .message_time = message_time1,
          .header = {},
          .data = message_data,
        });

      REQUIRE(writer.log_message_wait(logged_messages1.back(), message_handle, time1));
    }

    // Pause/Resume splits the log to a new file
    REQUIRE(writer.pause_logging(time1));
    REQUIRE(writer.resume_logging(time1));

    std::vector<Message> logged_messages2;

    for (uint32_t sequence_number = messages_per_file + 1U; sequence_number <= messages_per_file * 2U;
         ++sequence_number)
    {
      logged_messages2.emplace_back(
        Message{
          .channel_name = channel_name1,
          .sequence_number = sequence_number,
          .log_time = log_time1,
          .message_time = message_time1,
          .header = {},
          .data = message_data,
        });

      REQUIRE(writer.log_message_wait(logged_messages2.back(), message_handle, time1));
    }

    REQUIRE(writer.pause_logging(time1));
    REQUIRE(writer.drain_async_operations());
    REQUIRE(writer.get_pending_message_data_bytes() == 0U);

    const auto maybe_file1_data = tests::try_read_file((log_dir / "log_file_000000.olog").string());
    REQUIRE(maybe_file1_data);
    const auto file1_data = std::span{maybe_file1_data->data(), maybe_file1_data->size()};
    auto validate_result = tests::try_validate_log_header(file1_data);
    REQUIRE(validate_result);
    REQUIRE(validate_result.value() == 8U);
    validate_result = tests::try_validate_schema_record(file1_data, *validate_result, channel_metadata1, schema_id_map);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_channel_record(
      file1_data, *validate_result, channel_metadata1, schema_id_map, channel_id_map);
    REQUIRE(validate_result);
    for (const auto& logged_message : logged_messages1)
    {
      validate_result = tests::try_validate_pad_bytes(
        file1_data,
        *validate_result,
        jewels::Aligner<TestWriterPolicy::alignment>::aligned_remainder(*validate_result + message_record_header_size));
      validate_result =
        tests::try_validate_message_record(file1_data, *validate_result, logged_message, channel_id_map);
      REQUIRE(validate_result);
    }
    validate_result = tests::try_validate_pad_bytes(
      file1_data,
      *validate_result,
      jewels::Aligner<TestWriterPolicy::alignment>::aligned_remainder(
        *validate_result + end_log_file_record_header_size + record_trailer_size));
    REQUIRE(validate_result);
    validate_result = tests::try_validate_end_log_file_record(
      file1_data, *validate_result, true, log_time1, log_time1, message_time1, message_time1);
    REQUIRE(validate_result == file1_data.size());

    const auto maybe_file2_data = tests::try_read_file((log_dir / "log_file_000001.olog").string());
    REQUIRE(maybe_file2_data);
    const auto file2_data = std::span{maybe_file2_data->data(), maybe_file2_data->size()};
    validate_result = tests::try_validate_log_header(file2_data);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_schema_record(file2_data, *validate_result, channel_metadata1, schema_id_map);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_channel_record(
      file2_data, *validate_result, channel_metadata1, schema_id_map, channel_id_map);
    REQUIRE(validate_result);
    for (const auto& logged_message : logged_messages2)
    {
      validate_result = tests::try_validate_pad_bytes(
        file2_data,
        *validate_result,
        jewels::Aligner<TestWriterPolicy::alignment>::aligned_remainder(*validate_result + message_record_header_size));
      validate_result =
        tests::try_validate_message_record(file2_data, *validate_result, logged_message, channel_id_map);
      REQUIRE(validate_result);
    }
    REQUIRE(validate_result);
    validate_result = tests::try_validate_pad_bytes(
      file2_data,
      *validate_result,
      jewels::Aligner<TestWriterPolicy::alignment>::aligned_remainder(
        *validate_result + end_log_file_record_header_size + record_trailer_size));
    REQUIRE(validate_result);
    validate_result = tests::try_validate_end_log_file_record(
      file2_data, *validate_result, true, log_time1, log_time1, message_time1, message_time1);
    REQUIRE(validate_result == file2_data.size());
  }

  SECTION("Header too long")
  {
    Writer<TestWriterPolicy> writer{
      memory_resource, memory_resource, max_write_mib_per_sec, max_log_file_duration, WriterEnvironment::normal};
    REQUIRE(writer.open_log(log_dir.string(), log_file_prefix, time1));
    REQUIRE(writer.add_channel(channel_metadata1, time1));

    const size_t header_size1 = 65536U;
    const std::vector<std::byte> header1(header_size1, std::byte{'A'});
    const size_t message_size1 = 127U;
    auto message_result = message_buffer_pool.get_shared_buffer();
    REQUIRE(message_result);
    auto message_buffer1 = std::move(message_result).value();
    std::memset(message_buffer1->data(), 'B', message_size1);
    const std::span<std::byte> message1{message_buffer1->data(), message_size1};
    const uint32_t sequence_number1 = 1U;
    const LogTimestamp log_time1{std::chrono::nanoseconds(100)};
    const LogTimestamp message_time1{std::chrono::nanoseconds(1000)};
    REQUIRE(
      writer.log_message(
        Message{
          .channel_name = channel_name1,
          .sequence_number = sequence_number1,
          .log_time = log_time1,
          .message_time = message_time1,
          .header = {header1},
          .data = message1,
        },
        TestMessageHandleType{message_buffer1},
        time1) == jewels::unexpected(LogError::message_header_exceeds_max_size));
    REQUIRE(writer.get_state() == WriterState::degraded);
    REQUIRE(writer.get_status().status_string == "Message header exceeds max size");
  }

  SECTION("Record too long")
  {
    Writer<TestWriterPolicy> writer{
      memory_resource, memory_resource, max_write_mib_per_sec, max_log_file_duration, WriterEnvironment::normal};
    REQUIRE(writer.open_log(log_dir.string(), log_file_prefix, time1));
    REQUIRE(writer.add_channel(channel_metadata1, time1));

    const size_t header_size1 = 11U;
    const std::vector<std::byte> header1(header_size1, std::byte{'A'});
    const size_t message_size1 =
      max_log_record_size + 1U - header_size1 - message_record_header_size - sizeof(RecordTrailer);
    const std::vector<std::byte> message1(message_size1, std::byte{'B'});
    auto message_result = message_buffer_pool.get_shared_buffer();
    REQUIRE(message_result);
    auto message_buffer1 = std::move(message_result).value();
    const uint32_t sequence_number1 = 1U;
    const LogTimestamp log_time1{std::chrono::nanoseconds(100)};
    const LogTimestamp message_time1{std::chrono::nanoseconds(1000)};
    REQUIRE(
      writer.log_message(
        Message{
          .channel_name = channel_name1,
          .sequence_number = sequence_number1,
          .log_time = log_time1,
          .message_time = message_time1,
          .header = {header1},
          .data = {message1}},
        TestMessageHandleType{message_buffer1},
        time1) == jewels::unexpected(LogError::record_length_exceeds_max_record_size));
    REQUIRE(writer.get_state() == WriterState::degraded);
    REQUIRE(writer.get_status().status_string == "Message record length exceeds max size");
  }

  SECTION("Missing channel metadata")
  {
    Writer<TestWriterPolicy> writer{
      memory_resource, memory_resource, max_write_mib_per_sec, max_log_file_duration, WriterEnvironment::normal};
    REQUIRE(writer.open_log(log_dir.string(), log_file_prefix, time1));
    REQUIRE(writer.add_channel(channel_metadata1, time1));

    const size_t header_size1 = 11U;
    const std::vector<std::byte> header1(header_size1, std::byte{'A'});
    const size_t message_size1 = 127U;
    auto message_result = message_buffer_pool.get_shared_buffer();
    REQUIRE(message_result);
    auto message_buffer1 = std::move(message_result).value();
    std::memset(message_buffer1->data(), 'B', message_size1);
    const std::span<std::byte> message1{message_buffer1->data(), message_size1};
    const uint32_t sequence_number1 = 1U;
    const LogTimestamp log_time1{std::chrono::nanoseconds(100)};
    const LogTimestamp message_time1{std::chrono::nanoseconds(1000)};
    REQUIRE(
      writer.log_message(
        Message{
          .channel_name = "INVALID CHANNEL NAME",
          .sequence_number = sequence_number1,
          .log_time = log_time1,
          .message_time = message_time1,
          .header = {header1},
          .data = message1,
        },
        TestMessageHandleType{message_buffer1},
        time1) == jewels::unexpected(LogError::missing_channel_metadata));
    REQUIRE(writer.get_state() == WriterState::degraded);
    REQUIRE(writer.get_status().status_string == "Channel metadata for INVALID CHANNEL NAME is not configured");
  }
}

TEST_CASE("Error handlng")
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  static constexpr size_t message_buffer_count = 1U;
  static constexpr size_t max_write_mib_per_sec = 100U;
  static constexpr auto max_log_file_duration = std::chrono::seconds{0};
  const auto* log_file_prefix = "log_file_";

  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto log_dir = test_dir.get_path() / log_file_prefix;

  jewels::SharedBufferPool<message_buffer_size, message_alignment> message_buffer_pool{
    memory_resource, message_buffer_count};

  const auto* channel_name1 = "Channel 1";

  const jewels::time::SteadyTime time1{std::chrono::seconds(1)};

  const size_t header_size1 = 14U;
  const std::vector<std::byte> header1(header_size1, std::byte{'A'});
  const size_t message_size1 = 127U;
  auto message_result = message_buffer_pool.get_shared_buffer();
  REQUIRE(message_result);
  auto message_buffer1 = std::move(message_result).value();
  std::memset(message_buffer1->data(), 'B', message_size1);
  const std::span<std::byte> message1{message_buffer1->data(), message_size1};
  const uint32_t sequence_number1 = 1U;
  const LogTimestamp log_time1{std::chrono::nanoseconds(100)};
  const LogTimestamp message_time1{std::chrono::nanoseconds(1000)};

  SECTION("Log not open")
  {
    Writer<TestWriterPolicy> writer{
      memory_resource, memory_resource, max_write_mib_per_sec, max_log_file_duration, WriterEnvironment::normal};
    REQUIRE(writer.close_log(time1) == jewels::unexpected(LogError::not_open));
    REQUIRE(
      writer.log_message(
        Message{
          .channel_name = channel_name1,
          .sequence_number = sequence_number1,
          .log_time = log_time1,
          .message_time = message_time1,
          .header = {header1},
          .data = message1,
        },
        TestMessageHandleType{message_buffer1},
        time1) == jewels::unexpected(LogError::not_open));
    REQUIRE(writer.get_write_backlog(time1) == jewels::unexpected(LogError::not_open));
    REQUIRE(writer.get_log_file_offset() == jewels::unexpected(LogError::not_open));
    REQUIRE(writer.get_state() == WriterState::closed);
    REQUIRE(writer.get_status().status_string.empty());
  }

  SECTION("Already open")
  {
    Writer<TestWriterPolicy> writer{
      memory_resource, memory_resource, max_write_mib_per_sec, max_log_file_duration, WriterEnvironment::normal};
    REQUIRE(writer.open_log(log_dir.string(), log_file_prefix, time1));
    REQUIRE(writer.open_log(log_dir.string(), log_file_prefix, time1) == jewels::unexpected(LogError::already_open));
  }
}

} // namespace
} // namespace clockwork_logging::onboard
