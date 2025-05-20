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

TEST_CASE("Log persistent clockwork messages")
{
  constexpr size_t message_data_size = 1373U;
  constexpr size_t num_slots = 5U;

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
  const auto channel_type1 = ChannelType::persistent;

  const jewels::time::SteadyTime time1{std::chrono::seconds(1)};

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

  Writer<TestWriterPolicy> writer{
    memory_resource, memory_resource, max_write_mib_per_sec, max_log_file_duration, WriterEnvironment::normal};
  REQUIRE(writer.add_channel(channel_metadata1, time1));
  schema_id_map[schema_name1] = 1U;
  channel_id_map[channel_name1] = 1U;

  const LogTimestamp message_time1{std::chrono::nanoseconds(100)};
  const LogTimestamp log_time1{std::chrono::nanoseconds(101)};
  auto buffer_iterator1 = std::end(pinion_buffer);
  REQUIRE(pinion_buffer.increment_head(0U, 1UL));
  auto slot1 = buffer_iterator1.dereference();
  std::memset(slot1.message().data(), '\0', slot1.message().size());
  slot1.header()->publish_timestamp = message_time1.get_nanoseconds();
  slot1.header()->sequence_number = 1U;

  REQUIRE(writer.save_persistent_clockwork_message(
    channel_name1,
    ClockworkMessageHandle{jewels::memory::make_non_null_from_ref(pinion_buffer), buffer_iterator1},
    log_time1));

  REQUIRE(writer.open_log(log_dir.string(), log_file_prefix, time1));

  const LogTimestamp message_time2{std::chrono::nanoseconds(200)};
  const LogTimestamp log_time2{std::chrono::nanoseconds(201)};
  auto buffer_iterator2 = std::end(pinion_buffer);
  REQUIRE(pinion_buffer.increment_head(1U, 1UL));
  auto slot2 = buffer_iterator2.dereference();
  std::memset(slot2.message().data(), 'B', slot2.message().size());
  slot2.header()->publish_timestamp = message_time2.get_nanoseconds();
  slot2.header()->sequence_number = 2U;

  REQUIRE(writer.log_clockwork_message_wait(
    channel_name1,
    ClockworkMessageHandle{jewels::memory::make_non_null_from_ref(pinion_buffer), buffer_iterator2},
    log_time2,
    time1));

  REQUIRE(writer.pause_logging(time1));

  const LogTimestamp message_time3{std::chrono::nanoseconds(300)};
  const LogTimestamp log_time3{std::chrono::nanoseconds(301)};
  auto buffer_iterator3 = std::end(pinion_buffer);
  REQUIRE(pinion_buffer.increment_head(2U, 1UL));
  auto slot3 = buffer_iterator3.dereference();
  std::memset(slot3.message().data(), 'C', slot3.message().size());
  slot3.header()->publish_timestamp = message_time3.get_nanoseconds();
  slot3.header()->sequence_number = 3U;

  REQUIRE(writer.save_persistent_clockwork_message(
    channel_name1,
    ClockworkMessageHandle{jewels::memory::make_non_null_from_ref(pinion_buffer), buffer_iterator3},
    log_time3));

  const LogTimestamp message_time4{std::chrono::nanoseconds(400)};
  const LogTimestamp log_time4{std::chrono::nanoseconds(401)};
  auto buffer_iterator4 = std::end(pinion_buffer);
  REQUIRE(pinion_buffer.increment_head(3U, 1UL));
  auto slot4 = buffer_iterator4.dereference();
  std::memset(slot4.message().data(), '\0', slot4.message().size());
  slot4.header()->publish_timestamp = message_time4.get_nanoseconds();
  slot4.header()->sequence_number = 4U;

  REQUIRE(writer.save_persistent_clockwork_message(
    channel_name1,
    ClockworkMessageHandle{jewels::memory::make_non_null_from_ref(pinion_buffer), buffer_iterator4},
    log_time4));

  REQUIRE(writer.resume_logging(time1));

  const LogTimestamp message_time5{std::chrono::nanoseconds(500)};
  const LogTimestamp log_time5{std::chrono::nanoseconds(501)};
  auto buffer_iterator5 = std::end(pinion_buffer);
  REQUIRE(pinion_buffer.increment_head(4U, 1UL));
  auto slot5 = buffer_iterator5.dereference();
  std::memset(slot5.message().data(), 'E', slot5.message().size());
  slot5.header()->publish_timestamp = message_time5.get_nanoseconds();
  slot5.header()->sequence_number = 5U;

  REQUIRE(writer.log_clockwork_message_wait(
    channel_name1,
    ClockworkMessageHandle{jewels::memory::make_non_null_from_ref(pinion_buffer), buffer_iterator5},
    log_time5,
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
  validate_result =
    tests::try_validate_channel_record(file1_data, *validate_result, channel_metadata1, schema_id_map, channel_id_map);
  REQUIRE(validate_result);
  validate_result = tests::try_validate_message_record(
    file1_data,
    *validate_result,
    ZeroCopyMessage{
      .channel_name = channel_name1,
      .sequence_number = 1U,
      .log_time = log_time1,
      .message_time = message_time1,
      .header = {},
      .data = compressor.compress(slot1.message()),
    },
    channel_id_map,
    true,
    true,
    true);
  REQUIRE(validate_result);
  validate_result = tests::try_validate_message_record(
    file1_data,
    *validate_result,
    Message{
      .channel_name = channel_name1,
      .sequence_number = 2U,
      .log_time = log_time2,
      .message_time = message_time2,
      .header = {},
      .data = slot2.message(),
    },
    channel_id_map,
    false,
    true,
    false);
  REQUIRE(validate_result);
  validate_result = tests::try_validate_pad_bytes(
    file1_data,
    *validate_result,
    jewels::Aligner<TestWriterPolicy::alignment>::aligned_remainder(
      *validate_result + end_log_file_record_header_size + record_trailer_size));
  REQUIRE(validate_result);
  validate_result = tests::try_validate_end_log_file_record(
    file1_data, *validate_result, true, log_time2, log_time2, message_time2, message_time2);
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
    file2_data,
    *validate_result,
    ZeroCopyMessage{
      .channel_name = channel_name1,
      .sequence_number = 4U,
      .log_time = log_time4,
      .message_time = message_time4,
      .header = {},
      .data = compressor.compress(slot4.message()),
    },
    channel_id_map,
    true,
    true,
    true);
  REQUIRE(validate_result);
  validate_result = tests::try_validate_message_record(
    file2_data,
    *validate_result,
    Message{
      .channel_name = channel_name1,
      .sequence_number = 5U,
      .log_time = log_time5,
      .message_time = message_time5,
      .header = {},
      .data = slot5.message(),
    },
    channel_id_map,
    false,
    true,
    false);
  REQUIRE(validate_result);
  validate_result = tests::try_validate_pad_bytes(
    file2_data,
    *validate_result,
    jewels::Aligner<TestWriterPolicy::alignment>::aligned_remainder(
      *validate_result + end_log_file_record_header_size + record_trailer_size));
  REQUIRE(validate_result);
  validate_result = tests::try_validate_end_log_file_record(
    file2_data, *validate_result, true, log_time5, log_time5, message_time5, message_time5);
  REQUIRE(validate_result == file2_data.size());
}

} // namespace
} // namespace clockwork_logging::onboard
