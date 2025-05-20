// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/channel_type.hh"
#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/lite_compressor.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/message_encoding.hh"
#include "clockwork/logging/onboard/async_write_request.hh"
#include "clockwork/logging/onboard/async_writer.hh"
#include "clockwork/logging/onboard/clockwork_message_handle.hh"
#include "clockwork/logging/onboard/log_format.hh"
#include "clockwork/logging/onboard/null_message_handle.hh"
#include "clockwork/logging/onboard/tests/support/test_support.hh"
#include "clockwork/logging/onboard/types.hh"
#include "clockwork/logging/onboard/writer.hh"
#include "clockwork/logging/onboard/writer_state.hh"
#include "clockwork/logging/schema_encoding.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/tests/support/mock_buffer.hh"
#include "jewels/aligner/aligner.hh"
#include "jewels/math/constants.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/shared_pool/ref_counted_pool.hh"
#include "jewels/shared_pool/shared_buffer_pool.hh"
#include "jewels/shared_pool/shared_object_pool.hh"
#include "jewels/std/expected.hh"
#include "jewels/std/span.hh"
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/time/sync_time.hh"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iterator>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace clockwork_logging::onboard
{
namespace
{

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

TEST_CASE("Calculate buffer pool size, buffer pool size depends only on schema reserve buffers")
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
    REQUIRE(writer.close_log(time1));
    REQUIRE(writer.drain_async_operations());

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

    REQUIRE(writer.close_log(time1));
    REQUIRE(writer.drain_async_operations());

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
    REQUIRE(writer.close_log(time1));
    REQUIRE(writer.drain_async_operations());

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

TEST_CASE("Log clockwork messages")
{
  constexpr size_t message_data_size = 1373U;
  constexpr size_t num_slots = 3U;

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

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  LiteCompressor compressor{memory_resource};
  static constexpr size_t max_write_mib_per_sec = 100U;
  static constexpr auto max_log_file_duration = std::chrono::seconds{0};
  const auto* log_file_prefix = "log_file_";

  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto log_dir = test_dir.get_path() / log_file_prefix;

  const auto* schema_name1 = "Schema 1";
  const auto schema_encoding1 = SchemaEncoding::clockwork_tachyon;
  const auto* schema_desc1 = "Schema description 1";

  const auto* channel_name1 = "Channel 1";
  const auto compression_type1 = CompressionType::none;
  const auto message_encoding1 = MessageEncoding::tachyon;
  const auto channel_type1 = ChannelType::regular;

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

  SECTION("Single message")
  {
    Writer<TestWriterPolicy> writer{
      memory_resource, memory_resource, max_write_mib_per_sec, max_log_file_duration, WriterEnvironment::normal};
    REQUIRE(writer.open_log(log_dir.string(), log_file_prefix, time1));
    REQUIRE(writer.add_channel(channel_metadata1, time1));

    const uint32_t sequence_number1 = 1U;
    const LogTimestamp message_time1{std::chrono::nanoseconds(100)};
    const LogTimestamp log_time1{std::chrono::nanoseconds(101)};

    auto buffer_iterator1 = std::end(pinion_buffer);
    REQUIRE(pinion_buffer.increment_head(0U, 1UL));

    auto slot1 = buffer_iterator1.dereference();

    slot1.header()->sequence_number = sequence_number1;
    slot1.header()->publish_timestamp = message_time1.get_nanoseconds();
    std::memset(slot1.message().data(), '\0', 64U);
    std::memset(&slot1.message()[64U], 'B', slot1.message().size() - 64U);

    REQUIRE(writer.log_clockwork_message(
      channel_name1,
      ClockworkMessageHandle{jewels::memory::make_non_null_from_ref(pinion_buffer), buffer_iterator1},
      log_time1,
      time1));

    REQUIRE(writer.close_log(time1));
    REQUIRE(writer.drain_async_operations());

    SECTION("Continue writing into existing log")
    {
      const jewels::time::SteadyTime time2{std::chrono::seconds(2)};

      Writer<TestWriterPolicy> writer2{
        memory_resource, memory_resource, max_write_mib_per_sec, max_log_file_duration, WriterEnvironment::normal};
      REQUIRE(writer2.open_log(log_dir.string(), log_file_prefix, time2));
      REQUIRE(writer2.add_channel(channel_metadata1, time2));

      const uint32_t sequence_number2 = 2U;
      const LogTimestamp message_time2{std::chrono::nanoseconds(200)};
      const LogTimestamp log_time2{std::chrono::nanoseconds(201)};

      auto buffer_iterator2 = std::end(pinion_buffer);
      REQUIRE(pinion_buffer.increment_head(1U, 1UL));

      auto slot2 = buffer_iterator2.dereference();

      slot2.header()->sequence_number = sequence_number2;
      slot2.header()->publish_timestamp = message_time2.get_nanoseconds();
      std::memset(slot2.message().data(), 'C', slot2.message().size());

      REQUIRE(writer2.log_clockwork_message(
        channel_name1,
        ClockworkMessageHandle{jewels::memory::make_non_null_from_ref(pinion_buffer), buffer_iterator2},
        log_time2,
        time2));

      REQUIRE(writer2.close_log(time2));
      REQUIRE(writer2.drain_async_operations());

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
      validate_result = tests::try_validate_message_record(
        file_data,
        *validate_result,
        Message{
          .channel_name = channel_name1,
          .sequence_number = sequence_number2,
          .log_time = log_time2,
          .message_time = message_time2,
          .header = {},
          .data = slot2.message(),
        },
        channel_id_map);
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
    validate_result = tests::try_validate_message_record(
      file_data,
      *validate_result,
      ZeroCopyMessage{
        .channel_name = channel_name1,
        .sequence_number = sequence_number1,
        .log_time = log_time1,
        .message_time = message_time1,
        .header = {},
        .data = compressor.compress(slot1.message()),
      },
      channel_id_map,
      true);
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

    const jewels::time::SteadyTime time2 = time1 + TestWriterPolicy::max_write_backlog;
    const uint32_t sequence_number1 = 1U;
    const LogTimestamp message_time1{std::chrono::nanoseconds(100)};
    const LogTimestamp log_time1{std::chrono::nanoseconds(101)};

    auto buffer_iterator1 = std::end(pinion_buffer);
    REQUIRE(pinion_buffer.increment_head(0U, 1UL));

    auto slot1 = buffer_iterator1.dereference();

    slot1.header()->sequence_number = sequence_number1;
    slot1.header()->publish_timestamp = message_time1.get_nanoseconds();
    std::memset(slot1.message().data(), '\0', 64U);
    std::memset(&slot1.message()[64U], 'B', slot1.message().size() - 64U);

    REQUIRE(writer.log_clockwork_message(
      channel_name1,
      ClockworkMessageHandle{jewels::memory::make_non_null_from_ref(pinion_buffer), buffer_iterator1},
      log_time1,
      time2));

    const jewels::time::SteadyTime time3 = time2 + std::chrono::nanoseconds(1);
    const uint32_t sequence_number2 = 2U;
    const LogTimestamp message_time2{std::chrono::nanoseconds(200)};
    const LogTimestamp log_time2{std::chrono::nanoseconds(201)};

    auto buffer_iterator2 = std::end(pinion_buffer);
    REQUIRE(pinion_buffer.increment_head(1U, 1UL));

    auto slot2 = buffer_iterator2.dereference();

    slot2.header()->sequence_number = sequence_number2;
    slot2.header()->publish_timestamp = message_time2.get_nanoseconds();
    std::memset(slot2.message().data(), 'C', slot2.message().size());

    REQUIRE(
      writer.log_clockwork_message(
        channel_name1,
        ClockworkMessageHandle{jewels::memory::make_non_null_from_ref(pinion_buffer), buffer_iterator2},
        log_time2,
        time3) == jewels::unexpected(LogError::message_dropped));
    REQUIRE(writer.get_state() == WriterState::logging);
    REQUIRE(writer.get_and_reset_drop_count() == 1U);

    const jewels::time::SteadyTime time4 = time3 + std::chrono::seconds(1);
    writer.periodic_callback(time4);
    REQUIRE(writer.drain_async_operations());

    const uint32_t sequence_number3 = 3U;
    const LogTimestamp message_time3{std::chrono::nanoseconds(300)};
    const LogTimestamp log_time3{std::chrono::nanoseconds(301)};

    auto buffer_iterator3 = std::end(pinion_buffer);
    REQUIRE(pinion_buffer.increment_head(2U, 1UL));

    auto slot3 = buffer_iterator3.dereference();

    slot3.header()->sequence_number = sequence_number3;
    slot3.header()->publish_timestamp = message_time3.get_nanoseconds();
    std::memset(slot3.message().data(), '\0', 64U);
    std::memset(&slot3.message()[64U], 'D', slot3.message().size() - 64U);

    REQUIRE(writer.log_clockwork_message(
      channel_name1,
      ClockworkMessageHandle{jewels::memory::make_non_null_from_ref(pinion_buffer), buffer_iterator3},
      log_time3,
      time4));

    REQUIRE(writer.close_log(time4));
    REQUIRE(writer.drain_async_operations());

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
    validate_result = tests::try_validate_message_record(
      file_data,
      *validate_result,
      ZeroCopyMessage{
        .channel_name = channel_name1,
        .sequence_number = sequence_number1,
        .log_time = log_time1,
        .message_time = message_time1,
        .header = {},
        .data = compressor.compress(slot1.message()),
      },
      channel_id_map,
      true);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_pad_bytes(
      file_data, *validate_result, jewels::Aligner<TestWriterPolicy::alignment>::aligned_remainder(*validate_result));
    REQUIRE(validate_result);
    validate_result = tests::try_validate_message_record(
      file_data,
      *validate_result,
      ZeroCopyMessage{
        .channel_name = channel_name1,
        .sequence_number = sequence_number3,
        .log_time = log_time3,
        .message_time = message_time3,
        .header = {},
        .data = compressor.compress(slot3.message()),
      },
      channel_id_map,
      true);
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

  SECTION("Pinion buffer overwritten")
  {
    Writer<TestWriterPolicy> writer{
      memory_resource, memory_resource, max_write_mib_per_sec, max_log_file_duration, WriterEnvironment::normal};
    REQUIRE(writer.open_log(log_dir.string(), log_file_prefix, time1));
    REQUIRE(writer.add_channel(channel_metadata1, time1));

    const uint32_t sequence_number1 = 1U;
    const LogTimestamp message_time1{std::chrono::nanoseconds(100)};
    const LogTimestamp log_time1{std::chrono::nanoseconds(101)};

    auto buffer_iterator1 = std::end(pinion_buffer);
    REQUIRE(pinion_buffer.increment_head(0U, 1UL));

    auto slot1 = buffer_iterator1.dereference();

    slot1.header()->sequence_number = sequence_number1;
    slot1.header()->publish_timestamp = message_time1.get_nanoseconds();
    std::memset(slot1.message().data(), '\0', 64U);
    std::memset(&slot1.message()[64U], 'B', slot1.message().size() - 64U);

    REQUIRE(writer.log_clockwork_message(
      channel_name1,
      ClockworkMessageHandle{jewels::memory::make_non_null_from_ref(pinion_buffer), buffer_iterator1},
      log_time1,
      time1));

    const uint32_t sequence_number2 = 2U;
    const LogTimestamp message_time2{std::chrono::nanoseconds(200)};
    const LogTimestamp log_time2{std::chrono::nanoseconds(201)};

    auto buffer_iterator2 = std::end(pinion_buffer);
    REQUIRE(pinion_buffer.increment_head(1U, 1UL));

    auto slot2 = buffer_iterator2.dereference();

    slot2.header()->sequence_number = sequence_number2;
    slot2.header()->publish_timestamp = message_time2.get_nanoseconds();
    std::memset(slot2.message().data(), 'C', slot2.message().size());

    REQUIRE(writer.log_clockwork_message(
      channel_name1,
      ClockworkMessageHandle{jewels::memory::make_non_null_from_ref(pinion_buffer), buffer_iterator2},
      log_time2,
      time1));
    REQUIRE(writer.get_state() == WriterState::logging);

    REQUIRE(writer.get_and_reset_drop_count() == 0U);

    REQUIRE(pinion_buffer.increment_tail(0U, 1UL));
    REQUIRE(pinion_buffer.increment_tail(1U, 1UL));

    REQUIRE(
      writer.log_clockwork_message(
        channel_name1,
        ClockworkMessageHandle{jewels::memory::make_non_null_from_ref(pinion_buffer), buffer_iterator2},
        log_time2,
        time1) == jewels::unexpected(LogError::message_dropped));

    REQUIRE(writer.get_and_reset_drop_count() == 1U);

    REQUIRE(writer.get_state() == WriterState::logging);

    REQUIRE(writer.close_log(time1));
    REQUIRE(writer.drain_async_operations());

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
    validate_result = tests::try_validate_message_record(
      file_data,
      *validate_result,
      ZeroCopyMessage{
        .channel_name = channel_name1,
        .sequence_number = sequence_number1,
        .log_time = log_time1,
        .message_time = message_time1,
        .header = {},
        .data = compressor.compress(slot1.message()),
      },
      channel_id_map,
      true);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_message_record(
      file_data,
      *validate_result,
      Message{
        .channel_name = channel_name1,
        .sequence_number = sequence_number2,
        .log_time = log_time2,
        .message_time = message_time2,
        .header = {},
        .data = slot2.message(),
      },
      channel_id_map);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_pad_bytes(
      file_data,
      *validate_result,
      jewels::Aligner<TestWriterPolicy::alignment>::aligned_remainder(
        *validate_result + end_log_file_record_header_size + record_trailer_size));
    REQUIRE(validate_result);
    validate_result = tests::try_validate_end_log_file_record(
      file_data, *validate_result, true, log_time1, log_time2, message_time1, message_time2);
    REQUIRE(validate_result == file_data.size());
  }

  SECTION("Split files when max size is reached")
  {
    Writer<TestWriterPolicy> writer{
      memory_resource, memory_resource, max_write_mib_per_sec, max_log_file_duration, WriterEnvironment::normal};
    REQUIRE(writer.open_log(log_dir.string(), log_file_prefix, time1));
    REQUIRE(writer.add_channel(channel_metadata1, time1));

    const LogTimestamp message_time1{std::chrono::nanoseconds(100)};
    const LogTimestamp log_time1{std::chrono::nanoseconds(101)};
    const uint32_t sequence_number1 = 1U;

    auto buffer_iterator1 = std::end(pinion_buffer);
    REQUIRE(pinion_buffer.increment_head(0U, 1UL));

    auto slot1 = buffer_iterator1.dereference();

    std::memset(slot1.message().data(), '\0', 64U);
    std::memset(&slot1.message()[64U], 'B', slot1.message().size() - 64U);
    slot1.header()->sequence_number = sequence_number1;
    slot1.header()->publish_timestamp = message_time1.get_nanoseconds();

    const uint32_t messages_per_file = 191U;
    for (uint32_t i = 0U; i < messages_per_file; ++i)
    {
      REQUIRE(writer.log_clockwork_message_wait(
        channel_name1,
        ClockworkMessageHandle{jewels::memory::make_non_null_from_ref(pinion_buffer), buffer_iterator1},
        log_time1,
        time1));
    }

    // Next write splits to a new log file
    REQUIRE(writer.log_clockwork_message_wait(
      channel_name1,
      ClockworkMessageHandle{jewels::memory::make_non_null_from_ref(pinion_buffer), buffer_iterator1},
      log_time1,
      time1));

    REQUIRE(writer.close_log(time1));
    REQUIRE(writer.drain_async_operations());

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
    for (uint32_t i = 0U; i < messages_per_file; ++i)
    {
      CAPTURE(i);
      validate_result = tests::try_validate_message_record(
        file1_data,
        *validate_result,
        ZeroCopyMessage{
          .channel_name = channel_name1,
          .sequence_number = sequence_number1,
          .log_time = log_time1,
          .message_time = message_time1,
          .header = {},
          .data = compressor.compress(slot1.message()),
        },
        channel_id_map,
        true);
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
    validate_result = tests::try_validate_message_record(
      file2_data,
      *validate_result,
      ZeroCopyMessage{
        .channel_name = channel_name1,
        .sequence_number = sequence_number1,
        .log_time = log_time1,
        .message_time = message_time1,
        .header = {},
        .data = compressor.compress(slot1.message()),
      },
      channel_id_map,
      true);
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
    constexpr auto short_max_log_file_duration = std::chrono::seconds{2};
    Writer<TestWriterPolicy> writer{
      memory_resource, memory_resource, max_write_mib_per_sec, short_max_log_file_duration, WriterEnvironment::normal};
    REQUIRE(writer.open_log(log_dir.string(), log_file_prefix, time1));
    REQUIRE(writer.add_channel(channel_metadata1, time1));

    constexpr LogTimestamp message_time1{std::chrono::nanoseconds(100)};
    constexpr LogTimestamp log_time1{std::chrono::nanoseconds(101)};
    constexpr LogTimestamp log_time2{std::chrono::seconds{3}};
    constexpr uint32_t sequence_number1 = 1U;

    auto buffer_iterator1 = std::end(pinion_buffer);
    REQUIRE(pinion_buffer.increment_head(0U, 1UL));

    auto slot1 = buffer_iterator1.dereference();

    std::memset(slot1.message().data(), '\0', 64U);
    std::memset(&slot1.message()[64U], 'B', slot1.message().size() - 64U);
    slot1.header()->sequence_number = sequence_number1;
    slot1.header()->publish_timestamp = message_time1.get_nanoseconds();

    REQUIRE(writer.log_clockwork_message_wait(
      channel_name1,
      ClockworkMessageHandle{jewels::memory::make_non_null_from_ref(pinion_buffer), buffer_iterator1},
      log_time1,
      time1));

    REQUIRE(writer.log_clockwork_message_wait(
      channel_name1,
      ClockworkMessageHandle{jewels::memory::make_non_null_from_ref(pinion_buffer), buffer_iterator1},
      log_time2,
      time1));

    // Next write splits to a new log file
    REQUIRE(writer.log_clockwork_message_wait(
      channel_name1,
      ClockworkMessageHandle{jewels::memory::make_non_null_from_ref(pinion_buffer), buffer_iterator1},
      log_time2,
      time1));

    REQUIRE(writer.close_log(time1));
    REQUIRE(writer.drain_async_operations());

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

    validate_result = tests::try_validate_message_record(
      file1_data,
      *validate_result,
      ZeroCopyMessage{
        .channel_name = channel_name1,
        .sequence_number = sequence_number1,
        .log_time = log_time1,
        .message_time = message_time1,
        .header = {},
        .data = compressor.compress(slot1.message()),
      },
      channel_id_map,
      true);
    REQUIRE(validate_result);

    validate_result = tests::try_validate_message_record(
      file1_data,
      *validate_result,
      ZeroCopyMessage{
        .channel_name = channel_name1,
        .sequence_number = sequence_number1,
        .log_time = log_time2,
        .message_time = message_time1,
        .header = {},
        .data = compressor.compress(slot1.message()),
      },
      channel_id_map,
      true);
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
    validate_result = tests::try_validate_message_record(
      file2_data,
      *validate_result,
      ZeroCopyMessage{
        .channel_name = channel_name1,
        .sequence_number = sequence_number1,
        .log_time = log_time2,
        .message_time = message_time1,
        .header = {},
        .data = compressor.compress(slot1.message()),
      },
      channel_id_map,
      true);
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
    constexpr auto default_max_log_file_duration_s = std::chrono::seconds{0};
    Writer<TestWriterPolicy> writer{
      memory_resource,
      memory_resource,
      max_write_mib_per_sec,
      default_max_log_file_duration_s,
      WriterEnvironment::normal};
    REQUIRE(writer.open_log(log_dir.string(), log_file_prefix, time1));
    REQUIRE(writer.add_channel(channel_metadata1, time1));

    constexpr LogTimestamp message_time1{std::chrono::nanoseconds(100)};
    constexpr LogTimestamp log_time1{std::chrono::nanoseconds(101)};
    constexpr LogTimestamp log_time2{std::chrono::seconds{3}};
    constexpr uint32_t sequence_number1 = 1U;

    auto buffer_iterator1 = std::end(pinion_buffer);
    REQUIRE(pinion_buffer.increment_head(0U, 1UL));

    auto slot1 = buffer_iterator1.dereference();

    std::memset(slot1.message().data(), '\0', 64U);
    std::memset(&slot1.message()[64U], 'B', slot1.message().size() - 64U);
    slot1.header()->sequence_number = sequence_number1;
    slot1.header()->publish_timestamp = message_time1.get_nanoseconds();

    REQUIRE(writer.log_clockwork_message_wait(
      channel_name1,
      ClockworkMessageHandle{jewels::memory::make_non_null_from_ref(pinion_buffer), buffer_iterator1},
      log_time1,
      time1));

    REQUIRE(writer.log_clockwork_message_wait(
      channel_name1,
      ClockworkMessageHandle{jewels::memory::make_non_null_from_ref(pinion_buffer), buffer_iterator1},
      log_time2,
      time1));

    // Next write splits to a new log file
    REQUIRE(writer.log_clockwork_message_wait(
      channel_name1,
      ClockworkMessageHandle{jewels::memory::make_non_null_from_ref(pinion_buffer), buffer_iterator1},
      log_time2,
      time1));

    REQUIRE(writer.close_log(time1));
    REQUIRE(writer.drain_async_operations());

    const auto maybe_file1_data = tests::try_read_file((log_dir / "log_file_000000.olog").string());
    REQUIRE(maybe_file1_data);
    const auto file1_data = std::span{maybe_file1_data->data(), maybe_file1_data->size()};
    auto validate_result = tests::try_validate_log_header(file1_data);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_schema_record(file1_data, *validate_result, channel_metadata1, schema_id_map);
    REQUIRE(validate_result);
    validate_result = tests::try_validate_channel_record(
      file1_data, *validate_result, channel_metadata1, schema_id_map, channel_id_map);

    validate_result = tests::try_validate_message_record(
      file1_data,
      *validate_result,
      ZeroCopyMessage{
        .channel_name = channel_name1,
        .sequence_number = sequence_number1,
        .log_time = log_time1,
        .message_time = message_time1,
        .header = {},
        .data = compressor.compress(slot1.message()),
      },
      channel_id_map,
      true);
    REQUIRE(validate_result);

    validate_result = tests::try_validate_message_record(
      file1_data,
      *validate_result,
      ZeroCopyMessage{
        .channel_name = channel_name1,
        .sequence_number = sequence_number1,
        .log_time = log_time2,
        .message_time = message_time1,
        .header = {},
        .data = compressor.compress(slot1.message()),
      },
      channel_id_map,
      true);
    REQUIRE(validate_result);

    validate_result = tests::try_validate_message_record(
      file1_data,
      *validate_result,
      ZeroCopyMessage{
        .channel_name = channel_name1,
        .sequence_number = sequence_number1,
        .log_time = log_time2,
        .message_time = message_time1,
        .header = {},
        .data = compressor.compress(slot1.message()),
      },
      channel_id_map,
      true);
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

    const LogTimestamp message_time1{std::chrono::nanoseconds(100)};
    const LogTimestamp log_time1{std::chrono::nanoseconds(101)};
    const uint32_t sequence_number1 = 1U;

    auto buffer_iterator1 = std::end(pinion_buffer);
    REQUIRE(pinion_buffer.increment_head(0U, 1UL));

    auto slot1 = buffer_iterator1.dereference();

    std::memset(slot1.message().data(), '\0', 64U);
    std::memset(&slot1.message()[64U], 'B', slot1.message().size() - 64U);
    slot1.header()->sequence_number = sequence_number1;
    slot1.header()->publish_timestamp = message_time1.get_nanoseconds();

    const uint32_t messages_per_file = 5U;
    for (uint32_t i = 0U; i < messages_per_file; ++i)
    {
      REQUIRE(writer.log_clockwork_message_wait(
        channel_name1,
        ClockworkMessageHandle{jewels::memory::make_non_null_from_ref(pinion_buffer), buffer_iterator1},
        log_time1,
        time1));
    }

    // Pause/Resume splits the log to a new file
    REQUIRE(writer.pause_logging(time1));
    REQUIRE(writer.resume_logging(time1));

    for (uint32_t i = 0U; i < messages_per_file; ++i)
    {
      REQUIRE(writer.log_clockwork_message_wait(
        channel_name1,
        ClockworkMessageHandle{jewels::memory::make_non_null_from_ref(pinion_buffer), buffer_iterator1},
        log_time1,
        time1));
    }

    REQUIRE(writer.pause_logging(time1));
    REQUIRE(writer.drain_async_operations());

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
    for (uint32_t i = 0U; i < messages_per_file; ++i)
    {
      validate_result = tests::try_validate_message_record(
        file1_data,
        *validate_result,
        ZeroCopyMessage{
          .channel_name = channel_name1,
          .sequence_number = sequence_number1,
          .log_time = log_time1,
          .message_time = message_time1,
          .header = {},
          .data = compressor.compress(slot1.message()),
        },
        channel_id_map,
        true);
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
    for (uint32_t i = 0U; i < messages_per_file; ++i)
    {
      validate_result = tests::try_validate_message_record(
        file1_data,
        *validate_result,
        ZeroCopyMessage{
          .channel_name = channel_name1,
          .sequence_number = sequence_number1,
          .log_time = log_time1,
          .message_time = message_time1,
          .header = {},
          .data = compressor.compress(slot1.message()),
        },
        channel_id_map,
        true);
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

  SECTION("Missing channel metadata")
  {
    Writer<TestWriterPolicy> writer{
      memory_resource, memory_resource, max_write_mib_per_sec, max_log_file_duration, WriterEnvironment::normal};
    REQUIRE(writer.open_log(log_dir.string(), log_file_prefix, time1));
    REQUIRE(writer.add_channel(channel_metadata1, time1));

    const uint32_t sequence_number1 = 1U;
    const LogTimestamp message_time1{std::chrono::nanoseconds(100)};
    const LogTimestamp log_time1{std::chrono::nanoseconds(101)};

    auto buffer_iterator1 = std::end(pinion_buffer);
    REQUIRE(pinion_buffer.increment_head(0U, 1UL));

    auto slot1 = buffer_iterator1.dereference();

    slot1.header()->sequence_number = sequence_number1;
    slot1.header()->publish_timestamp = message_time1.get_nanoseconds();
    std::memset(slot1.message().data(), '\0', 64U);
    std::memset(&slot1.message()[64U], 'B', slot1.message().size() - 64U);

    REQUIRE(
      writer.log_clockwork_message(
        "INVALID CHANNEL NAME",
        ClockworkMessageHandle{jewels::memory::make_non_null_from_ref(pinion_buffer), buffer_iterator1},
        log_time1,
        time1) == jewels::unexpected(LogError::missing_channel_metadata));
    REQUIRE(writer.get_state() == WriterState::degraded);
    REQUIRE(writer.get_status().status_string == "Channel metadata for INVALID CHANNEL NAME is not configured");
  }
}

TEST_CASE("Error handlng")
{
  constexpr size_t message_data_size = 1373U;
  constexpr size_t num_slots = 1U;

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

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  static constexpr size_t max_write_mib_per_sec = 100U;
  static constexpr auto max_log_file_duration = std::chrono::seconds{0};
  const auto* log_file_prefix = "log_file_";

  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto log_dir = test_dir.get_path() / log_file_prefix;

  const auto* channel_name1 = "Channel 1";

  const jewels::time::SteadyTime time1{std::chrono::seconds(1)};

  const LogTimestamp message_time1{std::chrono::nanoseconds(100)};
  const LogTimestamp log_time1{std::chrono::nanoseconds(101)};
  const uint32_t sequence_number1 = 1U;

  auto buffer_iterator1 = std::end(pinion_buffer);
  REQUIRE(pinion_buffer.increment_head(0U, 1UL));

  auto slot1 = buffer_iterator1.dereference();

  std::memset(slot1.message().data(), '\0', 64U);
  std::memset(&slot1.message()[64U], 'B', slot1.message().size() - 64U);
  slot1.header()->sequence_number = sequence_number1;
  slot1.header()->publish_timestamp = message_time1.get_nanoseconds();

  SECTION("Log not open")
  {
    Writer<TestWriterPolicy> writer{
      memory_resource, memory_resource, max_write_mib_per_sec, max_log_file_duration, WriterEnvironment::normal};
    REQUIRE(writer.close_log(time1) == jewels::unexpected(LogError::not_open));
    REQUIRE(
      writer.log_clockwork_message(
        channel_name1,
        ClockworkMessageHandle{jewels::memory::make_non_null_from_ref(pinion_buffer), buffer_iterator1},
        log_time1,
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
