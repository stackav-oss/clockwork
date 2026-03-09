// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/channel_type_clk_cc.hh"
#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/message_encoding_clk_cc.hh"
#include "clockwork/logging/onboard/async_write_request.hh"
#include "clockwork/logging/onboard/async_writer.hh"
#include "clockwork/logging/onboard/buffered_reader.hh"
#include "clockwork/logging/onboard/log_format.hh"
#include "clockwork/logging/onboard/reader.hh"
#include "clockwork/logging/onboard/tests/support/test_message_handle.hh"
#include "clockwork/logging/onboard/tests/support/test_support.hh"
#include "clockwork/logging/onboard/types.hh"
#include "clockwork/logging/onboard/writer.hh"
#include "clockwork/logging/schema_encoding_clk_cc.hh"
#include "jewels/container/circular_buffer.hh"
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

#include <catch2/catch_test_macros.hpp>
#include <gsl/util>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <list>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace clockwork_logging::onboard::tests
{
namespace
{

/// Message buffer size
constexpr size_t message_buffer_size = jewels::math::constants::bytes_per_kib<size_t>;

/// Message buffer alignment
constexpr size_t message_alignment = 512U;

using TestMessageBufferType = jewels::SharedBufferPool<message_buffer_size, message_alignment>::SharedReference;

using TestMessageHandleType = TestMessageHandle<TestMessageBufferType>;

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
  static constexpr size_t header_size = 29U;
  static constexpr size_t max_write_mib_per_sec = 100U;
  static constexpr auto max_log_file_duration = std::chrono::seconds{0};

  static constexpr size_t message_buffer_count = 10U;
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
  const auto channel_type2 = ChannelType::persistent;

  jewels::SharedBufferPool<message_buffer_size, message_alignment> message_buffer_pool{
    memory_resource, message_buffer_count};

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
  auto message_result = message_buffer_pool.get_shared_buffer();
  REQUIRE(message_result);
  auto message_buffer0 = std::move(message_result).value();
  auto message_handle0 = TestMessageHandleType{message_buffer0};
  const auto message_data0 = std::span{*message_buffer0};
  fill_with_random_bytes(message_data0);

  REQUIRE(writer.save_persistent_message(
    Message{
      .channel_name = channel_name2,
      .sequence_number = 0U,
      .log_time = log_time0,
      .message_time = message_time0,
      .header = message_data0.first(header_size),
      .data = message_data0.last(message_buffer_size - header_size),
    },
    message_handle0));

  REQUIRE(writer.open_log(log_dir.string(), log_file_prefix, time1));

  constexpr auto log_time1 = log_time0 + message_interval;
  constexpr auto message_time1 = message_time0 + message_interval;
  message_result = message_buffer_pool.get_shared_buffer();
  REQUIRE(message_result);
  auto message_buffer1 = std::move(message_result).value();
  auto message_handle1 = TestMessageHandleType{message_buffer1};
  const auto message_data1 = std::span{*message_buffer1};
  fill_with_random_bytes(message_data1);

  REQUIRE(writer.log_message_wait(
    Message{
      .channel_name = channel_name1,
      .sequence_number = 1U,
      .log_time = log_time1,
      .message_time = message_time1,
      .header = message_data1.first(header_size),
      .data = message_data1.last(message_buffer_size - header_size),
    },
    message_handle1,
    time1));

  constexpr auto log_time2 = log_time1 + message_interval;
  constexpr auto message_time2 = message_time1 + message_interval;
  message_result = message_buffer_pool.get_shared_buffer();
  REQUIRE(message_result);
  auto message_buffer2 = std::move(message_result).value();
  auto message_handle2 = TestMessageHandleType{message_buffer2};
  const auto message_data2 = std::span{*message_buffer2};
  fill_with_random_bytes(message_data2);

  REQUIRE(writer.log_message_wait(
    Message{
      .channel_name = channel_name2,
      .sequence_number = 2U,
      .log_time = log_time2,
      .message_time = message_time2,
      .header = message_data2.first(header_size),
      .data = message_data2.last(message_buffer_size - header_size),
    },
    message_handle2,
    time1));

  constexpr auto log_time3 = log_time2 + message_interval;
  constexpr auto message_time3 = message_time2 + message_interval;
  message_result = message_buffer_pool.get_shared_buffer();
  REQUIRE(message_result);
  auto message_buffer3 = std::move(message_result).value();
  auto message_handle3 = TestMessageHandleType{message_buffer3};
  const auto message_data3 = std::span{*message_buffer3};
  fill_with_random_bytes(message_data3);

  REQUIRE(writer.log_message_wait(
    Message{
      .channel_name = channel_name1,
      .sequence_number = 3U,
      .log_time = log_time3,
      .message_time = message_time3,
      .header = message_data3.first(header_size),
      .data = message_data3.last(message_buffer_size - header_size),
    },
    message_handle3,
    time1));

  REQUIRE(writer.pause_logging(time1));

  constexpr auto log_time4 = log_time3 + message_interval;
  constexpr auto message_time4 = message_time3 + message_interval;
  message_result = message_buffer_pool.get_shared_buffer();
  REQUIRE(message_result);
  auto message_buffer4 = std::move(message_result).value();
  auto message_handle4 = TestMessageHandleType{message_buffer4};
  const auto message_data4 = std::span{*message_buffer4};
  fill_with_random_bytes(message_data4);

  REQUIRE(writer.save_persistent_message(
    Message{
      .channel_name = channel_name2,
      .sequence_number = 4U,
      .log_time = log_time0,
      .message_time = message_time4,
      .header = message_data4.first(header_size),
      .data = message_data4.last(message_buffer_size - header_size),
    },
    message_handle4));

  REQUIRE(writer.resume_logging(time1));

  constexpr auto log_time5 = log_time4 + message_interval;
  constexpr auto message_time5 = message_time4 + message_interval;
  message_result = message_buffer_pool.get_shared_buffer();
  REQUIRE(message_result);
  auto message_buffer5 = std::move(message_result).value();
  auto message_handle5 = TestMessageHandleType{message_buffer5};
  const auto message_data5 = std::span{*message_buffer5};
  fill_with_random_bytes(message_data5);

  REQUIRE(writer.log_message_wait(
    Message{
      .channel_name = channel_name2,
      .sequence_number = 5U,
      .log_time = log_time5,
      .message_time = message_time5,
      .header = message_data5.first(header_size),
      .data = message_data5.last(message_buffer_size - header_size),
    },
    message_handle5,
    time1));

  constexpr auto log_time6 = log_time5 + message_interval;
  constexpr auto message_time6 = message_time5 + message_interval;
  message_result = message_buffer_pool.get_shared_buffer();
  REQUIRE(message_result);
  auto message_buffer6 = std::move(message_result).value();
  auto message_handle6 = TestMessageHandleType{message_buffer6};
  const auto message_data6 = std::span{*message_buffer6};
  fill_with_random_bytes(message_data6);

  REQUIRE(writer.log_message_wait(
    Message{
      .channel_name = channel_name1,
      .sequence_number = 6U,
      .log_time = log_time6,
      .message_time = message_time6,
      .header = message_data6.first(header_size),
      .data = message_data6.last(message_buffer_size - header_size),
    },
    message_handle6,
    time1));

  REQUIRE(writer.pause_logging(time1));

  constexpr auto log_time7 = log_time6 + message_interval;
  constexpr auto message_time7 = message_time6 + message_interval;
  message_result = message_buffer_pool.get_shared_buffer();
  REQUIRE(message_result);
  auto message_buffer7 = std::move(message_result).value();
  auto message_handle7 = TestMessageHandleType{message_buffer7};
  const auto message_data7 = std::span{*message_buffer7};
  fill_with_random_bytes(message_data7);

  REQUIRE(writer.save_persistent_message(
    Message{
      .channel_name = channel_name2,
      .sequence_number = 7U,
      .log_time = log_time7,
      .message_time = message_time7,
      .header = message_data7.first(header_size),
      .data = message_data7.last(message_buffer_size - header_size),
    },
    message_handle7));

  REQUIRE(writer.resume_logging(time1));

  constexpr auto log_time8 = log_time7 + message_interval;
  constexpr auto message_time8 = message_time7 + message_interval;
  message_result = message_buffer_pool.get_shared_buffer();
  REQUIRE(message_result);
  auto message_buffer8 = std::move(message_result).value();
  auto message_handle8 = TestMessageHandleType{message_buffer8};
  const auto message_data8 = std::span{*message_buffer8};
  fill_with_random_bytes(message_data8);

  REQUIRE(writer.log_message_wait(
    Message{
      .channel_name = channel_name1,
      .sequence_number = 8U,
      .log_time = log_time8,
      .message_time = message_time8,
      .header = message_data8.first(header_size),
      .data = message_data8.last(message_buffer_size - header_size),
    },
    message_handle8,
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
    REQUIRE_FALSE(
      Reader<BufferedReader<TestReaderPolicy>>::get_file_log_interval(
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
    REQUIRE(std::ranges::equal(read_result->header, message_data0.first(header_size)));
    REQUIRE(std::ranges::equal(read_result->data, message_data0.last(message_buffer_size - header_size)));
    REQUIRE(read_result->message_type == LoggedMessageType::repeated_persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name1);
    REQUIRE(read_result->sequence_number == 1U);
    REQUIRE(read_result->log_time == log_time1);
    REQUIRE(read_result->message_time == message_time1);
    REQUIRE(std::ranges::equal(read_result->header, message_data1.first(header_size)));
    REQUIRE(std::ranges::equal(read_result->data, message_data1.last(message_buffer_size - header_size)));
    REQUIRE(read_result->message_type == LoggedMessageType::regular);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);
    REQUIRE(read_result->sequence_number == 2U);
    REQUIRE(read_result->log_time == log_time2);
    REQUIRE(read_result->message_time == message_time2);
    REQUIRE(std::ranges::equal(read_result->header, message_data2.first(header_size)));
    REQUIRE(std::ranges::equal(read_result->data, message_data2.last(message_buffer_size - header_size)));
    REQUIRE(read_result->message_type == LoggedMessageType::persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name1);
    REQUIRE(read_result->sequence_number == 3U);
    REQUIRE(read_result->log_time == log_time3);
    REQUIRE(read_result->message_time == message_time3);
    REQUIRE(std::ranges::equal(read_result->header, message_data3.first(header_size)));
    REQUIRE(std::ranges::equal(read_result->data, message_data3.last(message_buffer_size - header_size)));
    REQUIRE(read_result->message_type == LoggedMessageType::regular);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);
    REQUIRE(read_result->sequence_number == 5U);
    REQUIRE(read_result->log_time == log_time5);
    REQUIRE(read_result->message_time == message_time5);
    REQUIRE(std::ranges::equal(read_result->header, message_data5.first(header_size)));
    REQUIRE(std::ranges::equal(read_result->data, message_data5.last(message_buffer_size - header_size)));
    REQUIRE(read_result->message_type == LoggedMessageType::persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name1);
    REQUIRE(read_result->sequence_number == 6U);
    REQUIRE(read_result->log_time == log_time6);
    REQUIRE(read_result->message_time == message_time6);
    REQUIRE(std::ranges::equal(read_result->header, message_data6.first(header_size)));
    REQUIRE(std::ranges::equal(read_result->data, message_data6.last(message_buffer_size - header_size)));
    REQUIRE(read_result->message_type == LoggedMessageType::regular);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);
    REQUIRE(read_result->sequence_number == 7U);
    REQUIRE(read_result->log_time == log_time7);
    REQUIRE(read_result->message_time == message_time7);
    REQUIRE(std::ranges::equal(read_result->header, message_data7.first(header_size)));
    REQUIRE(std::ranges::equal(read_result->data, message_data7.last(message_buffer_size - header_size)));
    REQUIRE(read_result->message_type == LoggedMessageType::persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name1);
    REQUIRE(read_result->sequence_number == 8U);
    REQUIRE(read_result->log_time == log_time8);
    REQUIRE(read_result->message_time == message_time8);
    REQUIRE(std::ranges::equal(read_result->header, message_data8.first(header_size)));
    REQUIRE(std::ranges::equal(read_result->data, message_data8.last(message_buffer_size - header_size)));
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
    REQUIRE(std::ranges::equal(read_result->header, message_data2.first(header_size)));
    REQUIRE(std::ranges::equal(read_result->data, message_data2.last(message_buffer_size - header_size)));
    REQUIRE(read_result->message_type == LoggedMessageType::repeated_persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name1);
    REQUIRE(read_result->sequence_number == 3U);
    REQUIRE(read_result->log_time == log_time3);
    REQUIRE(read_result->message_time == message_time3);
    REQUIRE(std::ranges::equal(read_result->header, message_data3.first(header_size)));
    REQUIRE(std::ranges::equal(read_result->data, message_data3.last(message_buffer_size - header_size)));
    REQUIRE(read_result->message_type == LoggedMessageType::regular);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);
    REQUIRE(read_result->sequence_number == 5U);
    REQUIRE(read_result->log_time == log_time5);
    REQUIRE(read_result->message_time == message_time5);
    REQUIRE(std::ranges::equal(read_result->header, message_data5.first(header_size)));
    REQUIRE(std::ranges::equal(read_result->data, message_data5.last(message_buffer_size - header_size)));
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
    REQUIRE(std::ranges::equal(read_result->header, message_data5.first(header_size)));
    REQUIRE(std::ranges::equal(read_result->data, message_data5.last(message_buffer_size - header_size)));
    REQUIRE(read_result->message_type == LoggedMessageType::persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name1);
    REQUIRE(read_result->sequence_number == 6U);
    REQUIRE(read_result->log_time == log_time6);
    REQUIRE(read_result->message_time == message_time6);
    REQUIRE(std::ranges::equal(read_result->header, message_data6.first(header_size)));
    REQUIRE(std::ranges::equal(read_result->data, message_data6.last(message_buffer_size - header_size)));
    REQUIRE(read_result->message_type == LoggedMessageType::regular);

    REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));
  }
}

} // namespace
} // namespace clockwork_logging::onboard::tests
