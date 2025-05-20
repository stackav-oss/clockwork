// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/channel_type.hh"
#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/decompress_option.hh"
#include "clockwork/logging/lite_compressor.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/message_encoding.hh"
#include "clockwork/logging/onboard/buffered_reader.hh"
#include "clockwork/logging/onboard/log_format.hh"
#include "clockwork/logging/onboard/memory_writer.hh"
#include "clockwork/logging/onboard/reader.hh"
#include "clockwork/logging/onboard/tests/support/test_support.hh"
#include "clockwork/logging/onboard/types.hh"
#include "clockwork/logging/schema_encoding.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/filesystem/filesystem.hh"
#include "jewels/math/constants.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/tmp_directory_guard.hh"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <gsl/util>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <fcntl.h>
#include <filesystem>
#include <list>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace clockwork_logging::onboard::tests
{
namespace
{

/// Message buffer size
constexpr size_t message_buffer_size = jewels::math::constants::bytes_per_kib<size_t>;

struct TestReaderPolicy
{
  /// Filesystem library type
  using FilesystemType = jewels::filesystem::Filesystem;

  /// Read buffer size
  static constexpr size_t read_buffer_size = 2U * jewels::math::constants::bytes_per_mib<size_t>;

  /// Maximum read size
  static constexpr size_t max_read_size = max_log_record_size;

  /// Minimum size of reads when recovering from I/O error
  static constexpr size_t min_io_error_recover_read_size = 512U;
};

TEST_CASE("Log to memory")
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  static constexpr size_t header_size = 29U;

  const jewels::testing::TmpDirectoryGuard test_dir;

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

  MemoryWriter writer{memory_resource};
  LiteCompressor compressor{memory_resource};

  constexpr size_t log_buffer_size = 16384U;
  std::array<std::byte, log_buffer_size> log_buffer{};
  REQUIRE(writer.open_log(log_buffer));

  REQUIRE(writer.add_channel(LoggedChannelMetadata{
    channel_name1,
    compression_type1,
    message_encoding1,
    channel_type1,
    schema_name1,
    schema_encoding1,
    schema_definition1}));

  REQUIRE(writer.add_channel(LoggedChannelMetadata{
    channel_name2,
    compression_type2,
    message_encoding2,
    channel_type2,
    schema_name1,
    schema_encoding1,
    schema_definition1}));

  constexpr auto message_interval = std::chrono::milliseconds(10);

  constexpr LogTimestamp log_time0{std::chrono::milliseconds(1001)};
  constexpr LogTimestamp message_time0{std::chrono::milliseconds(1000)};
  auto message_buffer0 = std::vector<std::byte>(message_buffer_size);
  const auto message_data0 = std::span{message_buffer0};
  fill_with_random_bytes(message_data0);

  const bool is_lite_compressed = GENERATE(true, false);
  const auto decompress_option = GENERATE(DecompressOption::decompress, DecompressOption::dont_decompress);
  CAPTURE(is_lite_compressed, decompress_option);

  if (is_lite_compressed)
  {
    REQUIRE(writer.log_message(
      ZeroCopyMessage{
        .channel_name = channel_name2,
        .sequence_number = 0U,
        .log_time = log_time0,
        .message_time = message_time0,
        .header = message_data0.first(header_size),
        .data = compressor.compress(message_data0.last(message_buffer_size - header_size)),
      },
      true,
      true));
  }
  else
  {
    REQUIRE(writer.log_message(
      Message{
        .channel_name = channel_name2,
        .sequence_number = 0U,
        .log_time = log_time0,
        .message_time = message_time0,
        .header = message_data0.first(header_size),
        .data = message_data0.last(message_buffer_size - header_size),
      },
      false,
      true));
  }

  constexpr auto log_time1 = log_time0 + message_interval;
  constexpr auto message_time1 = message_time0 + message_interval;
  auto message_buffer1 = std::vector<std::byte>(message_buffer_size);
  const auto message_data1 = std::span{message_buffer1};
  fill_with_random_bytes(message_data1);

  if (is_lite_compressed)
  {
    REQUIRE(writer.log_message(
      ZeroCopyMessage{
        .channel_name = channel_name1,
        .sequence_number = 1U,
        .log_time = log_time1,
        .message_time = message_time1,
        .header = message_data1.first(header_size),
        .data = compressor.compress(message_data1.last(message_buffer_size - header_size)),
      },
      true,
      false));
  }
  else
  {
    REQUIRE(writer.log_message(
      Message{
        .channel_name = channel_name1,
        .sequence_number = 1U,
        .log_time = log_time1,
        .message_time = message_time1,
        .header = message_data1.first(header_size),
        .data = message_data1.last(message_buffer_size - header_size),
      },
      false,
      false));
  }

  constexpr auto log_time2 = log_time1 + message_interval;
  constexpr auto message_time2 = message_time1 + message_interval;
  auto message_buffer2 = std::vector<std::byte>(message_buffer_size);
  const auto message_data2 = std::span{message_buffer2};
  fill_with_random_bytes(message_data2);

  if (is_lite_compressed)
  {
    REQUIRE(writer.log_message(
      ZeroCopyMessage{
        .channel_name = channel_name2,
        .sequence_number = 2U,
        .log_time = log_time2,
        .message_time = message_time2,
        .header = message_data2.first(header_size),
        .data = compressor.compress(message_data2.last(message_buffer_size - header_size)),
      },
      true,
      false));
  }
  else
  {
    REQUIRE(writer.log_message(
      Message{
        .channel_name = channel_name2,
        .sequence_number = 2U,
        .log_time = log_time2,
        .message_time = message_time2,
        .header = message_data2.first(header_size),
        .data = message_data2.last(message_buffer_size - header_size),
      },
      false,
      false));
  }

  constexpr auto log_time3 = log_time2 + message_interval;
  constexpr auto message_time3 = message_time2 + message_interval;
  auto message_buffer3 = std::vector<std::byte>(message_buffer_size);
  const auto message_data3 = std::span{message_buffer3};
  fill_with_random_bytes(message_data3);

  if (is_lite_compressed)
  {
    REQUIRE(writer.log_message(
      ZeroCopyMessage{
        .channel_name = channel_name1,
        .sequence_number = 3U,
        .log_time = log_time3,
        .message_time = message_time3,
        .header = message_data3.first(header_size),
        .data = compressor.compress(message_data3.last(message_buffer_size - header_size)),
      },
      true,
      false));
  }
  else
  {
    REQUIRE(writer.log_message(
      Message{
        .channel_name = channel_name1,
        .sequence_number = 3U,
        .log_time = log_time3,
        .message_time = message_time3,
        .header = message_data3.first(header_size),
        .data = message_data3.last(message_buffer_size - header_size),
      },
      false,
      false));
  }

  constexpr auto log_time4 = log_time3 + message_interval;
  constexpr auto message_time4 = message_time3 + message_interval;
  auto message_buffer4 = std::vector<std::byte>(message_buffer_size);
  const auto message_data4 = std::span{message_buffer4};
  fill_with_random_bytes(message_data4);

  if (is_lite_compressed)
  {
    REQUIRE(writer.log_message(
      ZeroCopyMessage{
        .channel_name = channel_name2,
        .sequence_number = 4U,
        .log_time = log_time4,
        .message_time = message_time4,
        .header = message_data4.first(header_size),
        .data = compressor.compress(message_data4.last(message_buffer_size - header_size)),
      },
      true,
      false));
  }
  else
  {
    REQUIRE(writer.log_message(
      Message{
        .channel_name = channel_name2,
        .sequence_number = 4U,
        .log_time = log_time4,
        .message_time = message_time4,
        .header = message_data4.first(header_size),
        .data = message_data4.last(message_buffer_size - header_size),
      },
      false,
      false));
  }

  constexpr auto log_time5 = log_time4 + message_interval;
  constexpr auto message_time5 = message_time4 + message_interval;
  auto message_buffer5 = std::vector<std::byte>(message_buffer_size);
  const auto message_data5 = std::span{message_buffer5};
  fill_with_random_bytes(message_data5);

  if (is_lite_compressed)
  {
    REQUIRE(writer.log_message(
      ZeroCopyMessage{
        .channel_name = channel_name2,
        .sequence_number = 5U,
        .log_time = log_time5,
        .message_time = message_time5,
        .header = message_data5.first(header_size),
        .data = compressor.compress(message_data5.last(message_buffer_size - header_size)),
      },
      true,
      false));
  }
  else
  {
    REQUIRE(writer.log_message(
      Message{
        .channel_name = channel_name2,
        .sequence_number = 5U,
        .log_time = log_time5,
        .message_time = message_time5,
        .header = message_data5.first(header_size),
        .data = message_data5.last(message_buffer_size - header_size),
      },
      false,
      false));
  }

  constexpr auto log_time6 = log_time5 + message_interval;
  constexpr auto message_time6 = message_time5 + message_interval;
  auto message_buffer6 = std::vector<std::byte>(message_buffer_size);
  const auto message_data6 = std::span{message_buffer6};
  fill_with_random_bytes(message_data6);

  if (is_lite_compressed)
  {
    REQUIRE(writer.log_message(
      ZeroCopyMessage{
        .channel_name = channel_name1,
        .sequence_number = 6U,
        .log_time = log_time6,
        .message_time = message_time6,
        .header = message_data6.first(header_size),
        .data = compressor.compress(message_data6.last(message_buffer_size - header_size)),
      },
      true,
      false));
  }
  else
  {
    REQUIRE(writer.log_message(
      Message{
        .channel_name = channel_name1,
        .sequence_number = 6U,
        .log_time = log_time6,
        .message_time = message_time6,
        .header = message_data6.first(header_size),
        .data = message_data6.last(message_buffer_size - header_size),
      },
      false,
      false));
  }

  constexpr auto log_time7 = log_time6 + message_interval;
  constexpr auto message_time7 = message_time6 + message_interval;
  auto message_buffer7 = std::vector<std::byte>(message_buffer_size);
  const auto message_data7 = std::span{message_buffer7};
  fill_with_random_bytes(message_data7);

  if (is_lite_compressed)
  {
    REQUIRE(writer.log_message(
      ZeroCopyMessage{
        .channel_name = channel_name2,
        .sequence_number = 7U,
        .log_time = log_time7,
        .message_time = message_time7,
        .header = message_data7.first(header_size),
        .data = compressor.compress(message_data7.last(message_buffer_size - header_size)),
      },
      true,
      false));
  }
  else
  {
    REQUIRE(writer.log_message(
      Message{
        .channel_name = channel_name2,
        .sequence_number = 7U,
        .log_time = log_time7,
        .message_time = message_time7,
        .header = message_data7.first(header_size),
        .data = message_data7.last(message_buffer_size - header_size),
      },
      false,
      false));
  }

  constexpr auto log_time8 = log_time7 + message_interval;
  constexpr auto message_time8 = message_time7 + message_interval;
  auto message_buffer8 = std::vector<std::byte>(message_buffer_size);
  const auto message_data8 = std::span{message_buffer8};
  fill_with_random_bytes(message_data8);

  if (is_lite_compressed)
  {
    REQUIRE(writer.log_message(
      ZeroCopyMessage{
        .channel_name = channel_name1,
        .sequence_number = 8U,
        .log_time = log_time8,
        .message_time = message_time8,
        .header = message_data8.first(header_size),
        .data = compressor.compress(message_data8.last(message_buffer_size - header_size)),
      },
      true,
      false));
  }
  else
  {
    REQUIRE(writer.log_message(
      Message{
        .channel_name = channel_name1,
        .sequence_number = 8U,
        .log_time = log_time8,
        .message_time = message_time8,
        .header = message_data8.first(header_size),
        .data = message_data8.last(message_buffer_size - header_size),
      },
      false,
      false));
  }

  const auto close_result = writer.close_log();
  REQUIRE(close_result);

  const auto log_file_path = test_dir.get_path() / "memory.olog";
  jewels::filesystem::Filesystem filesys{memory_resource};
  auto open_result = filesys.open(log_file_path.string(), O_CREAT | O_EXCL | O_WRONLY);
  REQUIRE(open_result);
  REQUIRE(filesys.write(open_result.value(), close_result.value()));
  REQUIRE(open_result->close());

  SECTION("Get file log interval")
  {
    auto interval_result = Reader<BufferedReader<TestReaderPolicy>>::get_file_log_interval(
      memory_resource, (test_dir.get_path() / "memory.olog").string(), TimeFilterOption::log_time);
    REQUIRE(interval_result);
    REQUIRE(interval_result->get_start_timestamp() == log_time1);
    REQUIRE(interval_result->get_end_timestamp() == log_time8);
  }

  SECTION("Read all messages")
  {
    Reader<BufferedReader<TestReaderPolicy>> reader{
      memory_resource, test_dir.get_path().string(), MetadataMapOption::disable};
    REQUIRE(reader.open({}, TimeFilterOption::log_time, decompress_option));

    auto read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);
    REQUIRE(read_result->sequence_number == 0U);
    REQUIRE(read_result->log_time == log_time0);
    REQUIRE(read_result->message_time == message_time1);
    REQUIRE(std::ranges::equal(read_result->header, message_data0.first(header_size)));
    if (is_lite_compressed && decompress_option == DecompressOption::dont_decompress)
    {
      REQUIRE(read_result->is_lite_compressed);
      const auto decompress_result = compressor.decompress(read_result->data);
      REQUIRE(decompress_result);
      REQUIRE(std::ranges::equal(decompress_result.value(), message_data0.last(message_buffer_size - header_size)));
    }
    else
    {
      REQUIRE_FALSE(read_result->is_lite_compressed);
      REQUIRE(std::ranges::equal(read_result->data, message_data0.last(message_buffer_size - header_size)));
    }
    REQUIRE(read_result->message_type == LoggedMessageType::repeated_persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name1);
    REQUIRE(read_result->sequence_number == 1U);
    REQUIRE(read_result->log_time == log_time1);
    REQUIRE(read_result->message_time == message_time1);
    REQUIRE(std::ranges::equal(read_result->header, message_data1.first(header_size)));
    if (is_lite_compressed && decompress_option == DecompressOption::dont_decompress)
    {
      REQUIRE(read_result->is_lite_compressed);
      const auto decompress_result = compressor.decompress(read_result->data);
      REQUIRE(decompress_result);
      REQUIRE(std::ranges::equal(decompress_result.value(), message_data1.last(message_buffer_size - header_size)));
    }
    else
    {
      REQUIRE_FALSE(read_result->is_lite_compressed);
      REQUIRE(std::ranges::equal(read_result->data, message_data1.last(message_buffer_size - header_size)));
    }
    REQUIRE(read_result->message_type == LoggedMessageType::regular);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);
    REQUIRE(read_result->sequence_number == 2U);
    REQUIRE(read_result->log_time == log_time2);
    REQUIRE(read_result->message_time == message_time2);
    REQUIRE(std::ranges::equal(read_result->header, message_data2.first(header_size)));
    if (is_lite_compressed && decompress_option == DecompressOption::dont_decompress)
    {
      REQUIRE(read_result->is_lite_compressed);
      const auto decompress_result = compressor.decompress(read_result->data);
      REQUIRE(decompress_result);
      REQUIRE(std::ranges::equal(decompress_result.value(), message_data2.last(message_buffer_size - header_size)));
    }
    else
    {
      REQUIRE_FALSE(read_result->is_lite_compressed);
      REQUIRE(std::ranges::equal(read_result->data, message_data2.last(message_buffer_size - header_size)));
    }
    REQUIRE(read_result->message_type == LoggedMessageType::persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name1);
    REQUIRE(read_result->sequence_number == 3U);
    REQUIRE(read_result->log_time == log_time3);
    REQUIRE(read_result->message_time == message_time3);
    REQUIRE(std::ranges::equal(read_result->header, message_data3.first(header_size)));
    if (is_lite_compressed && decompress_option == DecompressOption::dont_decompress)
    {
      REQUIRE(read_result->is_lite_compressed);
      const auto decompress_result = compressor.decompress(read_result->data);
      REQUIRE(decompress_result);
      REQUIRE(std::ranges::equal(decompress_result.value(), message_data3.last(message_buffer_size - header_size)));
    }
    else
    {
      REQUIRE_FALSE(read_result->is_lite_compressed);
      REQUIRE(std::ranges::equal(read_result->data, message_data3.last(message_buffer_size - header_size)));
    }
    REQUIRE(read_result->message_type == LoggedMessageType::regular);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);
    REQUIRE(read_result->sequence_number == 4U);
    REQUIRE(read_result->log_time == log_time4);
    REQUIRE(read_result->message_time == message_time4);
    REQUIRE(std::ranges::equal(read_result->header, message_data4.first(header_size)));
    if (is_lite_compressed && decompress_option == DecompressOption::dont_decompress)
    {
      REQUIRE(read_result->is_lite_compressed);
      const auto decompress_result = compressor.decompress(read_result->data);
      REQUIRE(decompress_result);
      REQUIRE(std::ranges::equal(decompress_result.value(), message_data4.last(message_buffer_size - header_size)));
    }
    else
    {
      REQUIRE_FALSE(read_result->is_lite_compressed);
      REQUIRE(std::ranges::equal(read_result->data, message_data4.last(message_buffer_size - header_size)));
    }
    REQUIRE(read_result->message_type == LoggedMessageType::persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);
    REQUIRE(read_result->sequence_number == 5U);
    REQUIRE(read_result->log_time == log_time5);
    REQUIRE(read_result->message_time == message_time5);
    REQUIRE(std::ranges::equal(read_result->header, message_data5.first(header_size)));
    if (is_lite_compressed && decompress_option == DecompressOption::dont_decompress)
    {
      REQUIRE(read_result->is_lite_compressed);
      const auto decompress_result = compressor.decompress(read_result->data);
      REQUIRE(decompress_result);
      REQUIRE(std::ranges::equal(decompress_result.value(), message_data5.last(message_buffer_size - header_size)));
    }
    else
    {
      REQUIRE_FALSE(read_result->is_lite_compressed);
      REQUIRE(std::ranges::equal(read_result->data, message_data5.last(message_buffer_size - header_size)));
    }
    REQUIRE(read_result->message_type == LoggedMessageType::persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name1);
    REQUIRE(read_result->sequence_number == 6U);
    REQUIRE(read_result->log_time == log_time6);
    REQUIRE(read_result->message_time == message_time6);
    REQUIRE(std::ranges::equal(read_result->header, message_data6.first(header_size)));
    if (is_lite_compressed && decompress_option == DecompressOption::dont_decompress)
    {
      REQUIRE(read_result->is_lite_compressed);
      const auto decompress_result = compressor.decompress(read_result->data);
      REQUIRE(decompress_result);
      REQUIRE(std::ranges::equal(decompress_result.value(), message_data6.last(message_buffer_size - header_size)));
    }
    else
    {
      REQUIRE_FALSE(read_result->is_lite_compressed);
      REQUIRE(std::ranges::equal(read_result->data, message_data6.last(message_buffer_size - header_size)));
    }
    REQUIRE(read_result->message_type == LoggedMessageType::regular);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);
    REQUIRE(read_result->sequence_number == 7U);
    REQUIRE(read_result->log_time == log_time7);
    REQUIRE(read_result->message_time == message_time7);
    REQUIRE(std::ranges::equal(read_result->header, message_data7.first(header_size)));
    if (is_lite_compressed && decompress_option == DecompressOption::dont_decompress)
    {
      REQUIRE(read_result->is_lite_compressed);
      const auto decompress_result = compressor.decompress(read_result->data);
      REQUIRE(decompress_result);
      REQUIRE(std::ranges::equal(decompress_result.value(), message_data7.last(message_buffer_size - header_size)));
    }
    else
    {
      REQUIRE_FALSE(read_result->is_lite_compressed);
      REQUIRE(std::ranges::equal(read_result->data, message_data7.last(message_buffer_size - header_size)));
    }
    REQUIRE(read_result->message_type == LoggedMessageType::persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name1);
    REQUIRE(read_result->sequence_number == 8U);
    REQUIRE(read_result->log_time == log_time8);
    REQUIRE(read_result->message_time == message_time8);
    REQUIRE(std::ranges::equal(read_result->header, message_data8.first(header_size)));
    if (is_lite_compressed && decompress_option == DecompressOption::dont_decompress)
    {
      REQUIRE(read_result->is_lite_compressed);
      const auto decompress_result = compressor.decompress(read_result->data);
      REQUIRE(decompress_result);
      REQUIRE(std::ranges::equal(decompress_result.value(), message_data8.last(message_buffer_size - header_size)));
    }
    else
    {
      REQUIRE_FALSE(read_result->is_lite_compressed);
      REQUIRE(std::ranges::equal(read_result->data, message_data8.last(message_buffer_size - header_size)));
    }
    REQUIRE(read_result->message_type == LoggedMessageType::regular);

    REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));
  }

  SECTION("Read with log interval, first message is regular")
  {
    constexpr LogInterval log_interval{log_time3, log_time5};
    const auto list_result =
      Reader<BufferedReader<TestReaderPolicy>>::list_log_files(memory_resource, test_dir.get_path().string());
    REQUIRE(list_result);
    Reader<BufferedReader<TestReaderPolicy>> reader{memory_resource, list_result.value(), MetadataMapOption::disable};
    REQUIRE(reader.open(log_interval, TimeFilterOption::log_time, decompress_option));

    auto read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);
    REQUIRE(read_result->sequence_number == 2U);
    REQUIRE(read_result->log_time == log_time2);
    REQUIRE(read_result->message_time == message_time3);
    REQUIRE(std::ranges::equal(read_result->header, message_data2.first(header_size)));
    if (is_lite_compressed && decompress_option == DecompressOption::dont_decompress)
    {
      REQUIRE(read_result->is_lite_compressed);
      const auto decompress_result = compressor.decompress(read_result->data);
      REQUIRE(decompress_result);
      REQUIRE(std::ranges::equal(decompress_result.value(), message_data2.last(message_buffer_size - header_size)));
    }
    else
    {
      REQUIRE_FALSE(read_result->is_lite_compressed);
      REQUIRE(std::ranges::equal(read_result->data, message_data2.last(message_buffer_size - header_size)));
    }
    REQUIRE(read_result->message_type == LoggedMessageType::repeated_persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name1);
    REQUIRE(read_result->sequence_number == 3U);
    REQUIRE(read_result->log_time == log_time3);
    REQUIRE(read_result->message_time == message_time3);
    REQUIRE(std::ranges::equal(read_result->header, message_data3.first(header_size)));
    if (is_lite_compressed && decompress_option == DecompressOption::dont_decompress)
    {
      REQUIRE(read_result->is_lite_compressed);
      const auto decompress_result = compressor.decompress(read_result->data);
      REQUIRE(decompress_result);
      REQUIRE(std::ranges::equal(decompress_result.value(), message_data3.last(message_buffer_size - header_size)));
    }
    else
    {
      REQUIRE_FALSE(read_result->is_lite_compressed);
      REQUIRE(std::ranges::equal(read_result->data, message_data3.last(message_buffer_size - header_size)));
    }
    REQUIRE(read_result->message_type == LoggedMessageType::regular);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);
    REQUIRE(read_result->sequence_number == 4U);
    REQUIRE(read_result->log_time == log_time4);
    REQUIRE(read_result->message_time == message_time4);
    REQUIRE(std::ranges::equal(read_result->header, message_data4.first(header_size)));
    if (is_lite_compressed && decompress_option == DecompressOption::dont_decompress)
    {
      REQUIRE(read_result->is_lite_compressed);
      const auto decompress_result = compressor.decompress(read_result->data);
      REQUIRE(decompress_result);
      REQUIRE(std::ranges::equal(decompress_result.value(), message_data4.last(message_buffer_size - header_size)));
    }
    else
    {
      REQUIRE_FALSE(read_result->is_lite_compressed);
      REQUIRE(std::ranges::equal(read_result->data, message_data4.last(message_buffer_size - header_size)));
    }
    REQUIRE(read_result->message_type == LoggedMessageType::persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);
    REQUIRE(read_result->sequence_number == 5U);
    REQUIRE(read_result->log_time == log_time5);
    REQUIRE(read_result->message_time == message_time5);
    REQUIRE(std::ranges::equal(read_result->header, message_data5.first(header_size)));
    if (is_lite_compressed && decompress_option == DecompressOption::dont_decompress)
    {
      REQUIRE(read_result->is_lite_compressed);
      const auto decompress_result = compressor.decompress(read_result->data);
      REQUIRE(decompress_result);
      REQUIRE(std::ranges::equal(decompress_result.value(), message_data5.last(message_buffer_size - header_size)));
    }
    else
    {
      REQUIRE_FALSE(read_result->is_lite_compressed);
      REQUIRE(std::ranges::equal(read_result->data, message_data5.last(message_buffer_size - header_size)));
    }
    REQUIRE(read_result->message_type == LoggedMessageType::persistent);

    REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));
  }

  SECTION("Read with log interval, first message is persistent")
  {
    constexpr LogInterval log_interval{log_time5, log_time7};
    const auto list_result =
      Reader<BufferedReader<TestReaderPolicy>>::list_log_files(memory_resource, test_dir.get_path().string());
    REQUIRE(list_result);
    Reader<BufferedReader<TestReaderPolicy>> reader{memory_resource, list_result.value(), MetadataMapOption::disable};
    REQUIRE(reader.open(log_interval, TimeFilterOption::log_time, decompress_option));

    auto read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);
    REQUIRE(read_result->sequence_number == 5U);
    REQUIRE(read_result->log_time == log_time5);
    REQUIRE(read_result->message_time == message_time5);
    REQUIRE(std::ranges::equal(read_result->header, message_data5.first(header_size)));
    if (is_lite_compressed && decompress_option == DecompressOption::dont_decompress)
    {
      REQUIRE(read_result->is_lite_compressed);
      const auto decompress_result = compressor.decompress(read_result->data);
      REQUIRE(decompress_result);
      REQUIRE(std::ranges::equal(decompress_result.value(), message_data5.last(message_buffer_size - header_size)));
    }
    else
    {
      REQUIRE_FALSE(read_result->is_lite_compressed);
      REQUIRE(std::ranges::equal(read_result->data, message_data5.last(message_buffer_size - header_size)));
    }
    REQUIRE(read_result->message_type == LoggedMessageType::persistent);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name1);
    REQUIRE(read_result->sequence_number == 6U);
    REQUIRE(read_result->log_time == log_time6);
    REQUIRE(read_result->message_time == message_time6);
    REQUIRE(std::ranges::equal(read_result->header, message_data6.first(header_size)));
    if (is_lite_compressed && decompress_option == DecompressOption::dont_decompress)
    {
      REQUIRE(read_result->is_lite_compressed);
      const auto decompress_result = compressor.decompress(read_result->data);
      REQUIRE(decompress_result);
      REQUIRE(std::ranges::equal(decompress_result.value(), message_data6.last(message_buffer_size - header_size)));
    }
    else
    {
      REQUIRE_FALSE(read_result->is_lite_compressed);
      REQUIRE(std::ranges::equal(read_result->data, message_data6.last(message_buffer_size - header_size)));
    }
    REQUIRE(read_result->message_type == LoggedMessageType::regular);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);
    REQUIRE(read_result->sequence_number == 7U);
    REQUIRE(read_result->log_time == log_time7);
    REQUIRE(read_result->message_time == message_time7);
    REQUIRE(std::ranges::equal(read_result->header, message_data7.first(header_size)));
    if (is_lite_compressed && decompress_option == DecompressOption::dont_decompress)
    {
      REQUIRE(read_result->is_lite_compressed);
      const auto decompress_result = compressor.decompress(read_result->data);
      REQUIRE(decompress_result);
      REQUIRE(std::ranges::equal(decompress_result.value(), message_data7.last(message_buffer_size - header_size)));
    }
    else
    {
      REQUIRE_FALSE(read_result->is_lite_compressed);
      REQUIRE(std::ranges::equal(read_result->data, message_data7.last(message_buffer_size - header_size)));
    }
    REQUIRE(read_result->message_type == LoggedMessageType::persistent);

    REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));
  }

  SECTION("log buffer too small for open")
  {
    MemoryWriter writer2{memory_resource};
    REQUIRE(writer2.open_log({}) == jewels::unexpected(LogError::buffer_full));
  }

  SECTION("log buffer too small for add channel")
  {
    std::array<std::byte, 16U> log_buffer2{};
    MemoryWriter writer2{memory_resource};
    REQUIRE(writer2.open_log(log_buffer2));
    REQUIRE(
      writer2.add_channel(LoggedChannelMetadata{
        channel_name1,
        compression_type1,
        message_encoding1,
        channel_type1,
        schema_name1,
        schema_encoding1,
        schema_definition1}) == jewels::unexpected(LogError::buffer_full));
  }

  SECTION("log buffer too small for message")
  {
    std::array<std::byte, message_buffer_size> log_buffer2{};
    MemoryWriter writer2{memory_resource};
    REQUIRE(writer2.open_log(log_buffer2));
    REQUIRE(writer2.add_channel(LoggedChannelMetadata{
      channel_name1,
      compression_type1,
      message_encoding1,
      channel_type1,
      schema_name1,
      schema_encoding1,
      schema_definition1}));
    REQUIRE(
      writer2.log_message(
        Message{
          .channel_name = channel_name1,
          .sequence_number = 0U,
          .log_time = log_time0,
          .message_time = message_time0,
          .header = {},
          .data = message_data0,
        },
        false,
        false) == jewels::unexpected(LogError::buffer_full));
  }
}

} // namespace
} // namespace clockwork_logging::onboard::tests
