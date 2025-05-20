// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/channel_type.hh"
#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/message_encoding.hh"
#include "clockwork/logging/onboard/async_write_request.hh"
#include "clockwork/logging/onboard/async_writer.hh"
#include "clockwork/logging/onboard/log_format.hh"
#include "clockwork/logging/onboard/tests/support/test_message_handle.hh"
#include "clockwork/logging/onboard/tests/support/test_support.hh"
#include "clockwork/logging/onboard/types.hh"
#include "clockwork/logging/onboard/writer.hh"
#include "clockwork/logging/schema_encoding.hh"
#include "jewels/aligner/aligner.hh"
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
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
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

TEST_CASE("Log persistent messages")
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  static constexpr size_t max_write_mib_per_sec = 100U;
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
  const auto channel_type1 = ChannelType::persistent;

  std::unordered_map<std::string_view, uint16_t> schema_id_map;
  std::unordered_map<std::string_view, uint16_t> channel_id_map;

  const LoggedChannelMetadata channel_metadata1{
    .channel_name = channel_name1,
    .compression_type = compression_type1,
    .message_encoding = message_encoding1,
    .channel_type = channel_type1,
    .schema_name = schema_name1,
    .schema_encoding = schema_encoding1,
    .schema_definition = schema_desc1,
  };

  jewels::SharedBufferPool<message_buffer_size, message_alignment> message_buffer_pool{
    memory_resource, message_buffer_count};

  const jewels::time::SteadyTime time1{std::chrono::seconds(1)};

  Writer<TestWriterPolicy> writer{
    memory_resource, memory_resource, max_write_mib_per_sec, max_log_file_duration, WriterEnvironment::normal};

  REQUIRE(writer.add_channel(channel_metadata1, time1));
  schema_id_map[schema_name1] = 1U;
  channel_id_map[channel_name1] = 1U;

  auto message_result = message_buffer_pool.get_shared_buffer();
  REQUIRE(message_result);
  auto message_buffer = std::move(message_result).value();
  auto message_handle = TestMessageHandleType{message_buffer};
  std::memset(message_buffer->data(), 'B', message_buffer->size());
  const auto message_data = std::span{*message_buffer};

  constexpr LogTimestamp log_time1{std::chrono::nanoseconds(100)};
  constexpr LogTimestamp message_time1{std::chrono::nanoseconds(1000)};

  const Message logged_message1{
    .channel_name = channel_name1,
    .sequence_number = 1U,
    .log_time = log_time1,
    .message_time = message_time1,
    .header = {},
    .data = message_data,
  };

  REQUIRE(writer.save_persistent_message(logged_message1, message_handle));

  REQUIRE(writer.open_log(log_dir.string(), log_file_prefix, time1));

  const Message logged_message2{
    .channel_name = channel_name1,
    .sequence_number = 2U,
    .log_time = log_time1,
    .message_time = message_time1,
    .header = {},
    .data = message_data,
  };

  REQUIRE(writer.log_message_wait(logged_message2, message_handle, time1));

  REQUIRE(writer.pause_logging(time1));

  const Message logged_message3{
    .channel_name = channel_name1,
    .sequence_number = 3U,
    .log_time = log_time1,
    .message_time = message_time1,
    .header = {},
    .data = message_data,
  };

  REQUIRE(writer.save_persistent_message(logged_message3, message_handle));

  const Message logged_message4{
    .channel_name = channel_name1,
    .sequence_number = 4U,
    .log_time = log_time1,
    .message_time = message_time1,
    .header = {},
    .data = message_data,
  };

  REQUIRE(writer.save_persistent_message(logged_message4, message_handle));

  REQUIRE(writer.resume_logging(time1));

  const Message logged_message5{
    .channel_name = channel_name1,
    .sequence_number = 5U,
    .log_time = log_time1,
    .message_time = message_time1,
    .header = {},
    .data = message_data,
  };

  REQUIRE(writer.log_message_wait(logged_message5, message_handle, time1));

  REQUIRE(writer.close_log(time1));
  REQUIRE(writer.drain_async_operations());

  const auto maybe_file1_data = tests::try_read_file((log_dir / "log_file_000000.olog").string());
  REQUIRE(maybe_file1_data);
  const auto file1_data = std::span{maybe_file1_data->data(), maybe_file1_data->size()};
  auto validate_result = tests::try_validate_log_header(file1_data);
  REQUIRE(validate_result);
  validate_result = tests::try_validate_schema_record(file1_data, *validate_result, channel_metadata1, schema_id_map);
  REQUIRE(validate_result);
  validate_result =
    tests::try_validate_channel_record(file1_data, *validate_result, channel_metadata1, schema_id_map, channel_id_map);
  REQUIRE(validate_result);
  validate_result = tests::try_validate_message_record(
    file1_data, *validate_result, logged_message1, channel_id_map, false, true, true);
  REQUIRE(validate_result);
  validate_result = tests::try_validate_pad_bytes(
    file1_data,
    *validate_result,
    jewels::Aligner<TestWriterPolicy::alignment>::aligned_remainder(*validate_result + message_record_header_size));
  validate_result = tests::try_validate_message_record(
    file1_data, *validate_result, logged_message2, channel_id_map, false, true, false);
  REQUIRE(validate_result);
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
  validate_result =
    tests::try_validate_channel_record(file2_data, *validate_result, channel_metadata1, schema_id_map, channel_id_map);
  REQUIRE(validate_result);
  validate_result = tests::try_validate_message_record(
    file2_data, *validate_result, logged_message4, channel_id_map, false, true, true);
  REQUIRE(validate_result);
  validate_result = tests::try_validate_pad_bytes(
    file2_data,
    *validate_result,
    jewels::Aligner<TestWriterPolicy::alignment>::aligned_remainder(*validate_result + message_record_header_size));
  REQUIRE(validate_result);
  validate_result = tests::try_validate_message_record(
    file2_data, *validate_result, logged_message5, channel_id_map, false, true, false);
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

} // namespace
} // namespace clockwork_logging::onboard
