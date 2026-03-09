// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/channel_type_clk_cc.hh"
#include "clockwork/logging/lite_compressor.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/message_encoding_clk_cc.hh"
#include "clockwork/logging/offboard/reader.hh"
#include "clockwork/logging/offboard/tests/support/test_support.hh"
#include "clockwork/logging/offboard/types.hh"
#include "clockwork/logging/offboard/writer.hh"
#include "clockwork/logging/onboard/tests/support/test_support.hh"
#include "clockwork/logging/schema_encoding_clk_cc.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/filesystem.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/default_memory_resource.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/tmp_directory_guard.hh"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <memory_resource>
#include <optional>
#include <ratio>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace clockwork_logging::offboard
{
namespace
{

TEST_CASE("Reader")
{
  constexpr auto test_log_name = "test_log";

  // Configure writer to put each channel in a separate file
  constexpr auto writer_config_text = R"(
    # proto-file: clockwork/logging/offboard/v1/writer_config.proto
    # proto-message: WriterConfig
    rule {
      regex: ".*"
    }
  )";

  const auto message_chunk_index_format = GENERATE(MessageChunkIndexFormat::v1, MessageChunkIndexFormat::v2);
  CAPTURE(message_chunk_index_format);
  const auto recover_metadata = GENERATE(false, true);
  CAPTURE(recover_metadata);

  const auto memory_resource = jewels::memory::get_default_memory_resource();
  const jewels::filesystem::Filesystem vfs{memory_resource};
  LiteCompressor lite_compressor{memory_resource};
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto test_log_path = test_dir.get_path() / test_log_name;
  Writer writer{memory_resource, message_chunk_index_format};

  constexpr auto channel_name1 = "channel1";
  constexpr auto metadata1 = LoggedChannelMetadata{
    .channel_name = channel_name1,
    .message_encoding = MessageEncoding::unspecified,
    .channel_type = ChannelType::regular,
    .schema_name = "schema1",
    .schema_encoding = SchemaEncoding::undefined,
    .schema_definition = "Schema definition 1",
  };
  constexpr auto header1_size = 123U;
  std::vector<std::byte> header1(header1_size);
  onboard::tests::fill_with_random_bytes(header1);
  constexpr auto data1_size = 1234U;
  std::vector<std::byte> data1(data1_size);
  onboard::tests::fill_with_random_bytes(data1);

  constexpr auto channel_name2 = "channel2";
  constexpr auto metadata2 = LoggedChannelMetadata{
    .channel_name = channel_name2,
    .message_encoding = MessageEncoding::unspecified,
    .channel_type = ChannelType::persistent,
    .schema_name = "schema2",
    .schema_encoding = SchemaEncoding::undefined,
    .schema_definition = "Schema definition 2",
  };
  constexpr auto header2_size = 234U;
  std::vector<std::byte> header2(header2_size);
  onboard::tests::fill_with_random_bytes(header2);
  constexpr auto data2_size = 2345U;
  std::vector<std::byte> data2(data2_size);
  onboard::tests::fill_with_random_bytes(data2);
  const auto compressed_data2 = offboard::tests::lite_compress(data2, lite_compressor);

  constexpr auto channel_name3 = "channel3";
  constexpr auto metadata3 = LoggedChannelMetadata{
    .channel_name = channel_name3,
    .message_encoding = MessageEncoding::unspecified,
    .channel_type = ChannelType::persistent,
    .schema_name = "schema3",
    .schema_encoding = SchemaEncoding::undefined,
    .schema_definition = "Schema definition 3",
  };

  SECTION("Empty log")
  {
    REQUIRE(writer.open(test_log_path.string(), writer_config_text));
    REQUIRE(writer.create_channel(metadata1));
    REQUIRE(writer.create_channel(metadata2));
    REQUIRE(writer.create_channel(metadata3));
    REQUIRE(writer.close());

    if (recover_metadata)
    {
      REQUIRE(vfs.remove(test_log_path / "stack_log_metadata.pbtxt").has_value());
    }

    Reader reader{memory_resource, test_log_path.string()};

    const auto metadata_result = reader.get_metadata();
    REQUIRE(metadata_result);
    REQUIRE((*metadata_result)->size() == 3U);
    REQUIRE((*metadata_result)->at(channel_name1) == metadata1);
    REQUIRE((*metadata_result)->at(channel_name2) == metadata2);
    REQUIRE((*metadata_result)->at(channel_name3) == metadata3);

    const auto channel1_metadata_result = reader.get_channel_metadata(channel_name1);
    REQUIRE(channel1_metadata_result);
    REQUIRE(channel1_metadata_result.value() == metadata1);

    const auto channel2_metadata_result = reader.get_channel_metadata(channel_name2);
    REQUIRE(channel2_metadata_result);
    REQUIRE(channel1_metadata_result.value() == metadata1);

    REQUIRE(reader.get_channel_metadata("not_a_channel") == jewels::unexpected(LogError::unknown_channel));

    const auto channels_result = reader.get_channels();
    REQUIRE(channels_result);
    REQUIRE((*channels_result)->size() == 3U);
    REQUIRE((*channels_result)->contains(channel_name1));
    REQUIRE((*channels_result)->contains(channel_name2));
    REQUIRE((*channels_result)->contains(channel_name3));

    REQUIRE(reader.get_log_interval().value() == LogInterval{LogTimestamp{0}, LogTimestamp{0}});

    const auto metrics_result = reader.get_metrics();
    REQUIRE(metrics_result);
    REQUIRE((*metrics_result)->message_count == 0U);
    REQUIRE((*metrics_result)->byte_count == 0U);
    REQUIRE((*metrics_result)->metrics_map.empty());
    REQUIRE((*metrics_result)->transmit_time_interval == LogInterval{LogTimestamp{0}, LogTimestamp{0}});

    REQUIRE(reader.open());
    REQUIRE_FALSE(reader);
    REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));
  }

  SECTION("Log with data")
  {
    REQUIRE(writer.open(test_log_path.string(), writer_config_text));
    REQUIRE(writer.create_channel(metadata1));
    REQUIRE(writer.create_channel(metadata2));
    REQUIRE(writer.create_channel(metadata3));

    const uint32_t message_count = 20000U;
    const LogTimestamp start_time{std::chrono::seconds{1'000'000}};
    const std::chrono::seconds message_interval{1};
    auto transmit_time = start_time;
    for (uint32_t i = 0U; i < message_count; ++i)
    {
      REQUIRE(writer.write(
        LoggedMessage{
          .channel_name = channel_name1,
          .sequence_number = i * 2U,
          .log_time = transmit_time + std::chrono::nanoseconds(1),
          .transmit_time = transmit_time,
          .header = header1,
          .data = data1,
          .is_repeated_persistent = false,
          .is_lite_compressed = false,
        }));
      transmit_time -= message_interval;

      REQUIRE(writer.write(
        LoggedMessage{
          .channel_name = channel_name2,
          .sequence_number = (i * 2U) + 1U,
          .log_time = transmit_time + std::chrono::nanoseconds(1),
          .transmit_time = transmit_time,
          .header = header2,
          .data = compressed_data2,
          .is_repeated_persistent = false,
          .is_lite_compressed = true,
        }));
      transmit_time -= message_interval;
    }
    const auto end_time = transmit_time + message_interval;

    REQUIRE(writer.close());

    if (recover_metadata)
    {
      REQUIRE(vfs.remove(test_log_path / "stack_log_metadata.pbtxt").has_value());
    }

    Reader reader{memory_resource, test_log_path.string()};

    const auto metadata_result = reader.get_metadata();
    REQUIRE(metadata_result);
    REQUIRE((*metadata_result)->size() == 3U);
    REQUIRE((*metadata_result)->at(channel_name1) == metadata1);
    REQUIRE((*metadata_result)->at(channel_name2) == metadata2);
    REQUIRE((*metadata_result)->at(channel_name3) == metadata3);

    const auto channel1_metadata_result = reader.get_channel_metadata(channel_name1);
    REQUIRE(channel1_metadata_result);
    REQUIRE(channel1_metadata_result.value() == metadata1);

    const auto channel2_metadata_result = reader.get_channel_metadata(channel_name2);
    REQUIRE(channel2_metadata_result);
    REQUIRE(channel1_metadata_result.value() == metadata1);

    REQUIRE(reader.get_channel_metadata("not_a_channel") == jewels::unexpected(LogError::unknown_channel));

    const auto channels_result = reader.get_channels();
    REQUIRE(channels_result);
    REQUIRE((*channels_result)->size() == 3U);
    REQUIRE((*channels_result)->contains(channel_name1));
    REQUIRE((*channels_result)->contains(channel_name2));
    REQUIRE((*channels_result)->contains(channel_name3));

    REQUIRE(reader.get_log_interval().value() == LogInterval{start_time, end_time});

    const auto metrics_result = reader.get_metrics();
    REQUIRE(metrics_result);
    REQUIRE((*metrics_result)->message_count == message_count * 2U);
    REQUIRE(
      (*metrics_result)->byte_count ==
      message_count * (data1.size() + compressed_data2.size() + header1.size() + header2.size()));
    REQUIRE((*metrics_result)->transmit_time_interval == LogInterval{start_time, end_time});
    REQUIRE((*metrics_result)->metrics_map.size() == 2U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name1).message_count == message_count);
    REQUIRE(
      (*metrics_result)->metrics_map.at(channel_name1).byte_count == message_count * (data1.size() + header1.size()));
    REQUIRE(
      (*metrics_result)->metrics_map.at(channel_name1).transmit_time_interval ==
      LogInterval{start_time, end_time + message_interval});
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).message_count == message_count);
    REQUIRE(
      (*metrics_result)->metrics_map.at(channel_name2).byte_count ==
      message_count * (compressed_data2.size() + header2.size()));
    REQUIRE(
      (*metrics_result)->metrics_map.at(channel_name2).transmit_time_interval ==
      LogInterval{start_time - message_interval, end_time});

    SECTION("Read entire log")
    {
      REQUIRE(reader.open());
      REQUIRE(reader);

      auto expected_sequence_number = message_count * 2U;
      auto expected_time = end_time;
      for (size_t i = 0U; i < message_count; ++i)
      {
        CAPTURE(i);

        --expected_sequence_number;
        auto read_result = reader.read_next();
        REQUIRE(read_result);
        REQUIRE(read_result->channel_name == channel_name2);
        REQUIRE(read_result->sequence_number == expected_sequence_number);
        REQUIRE(read_result->log_time == expected_time + std::chrono::nanoseconds(1));
        REQUIRE(read_result->transmit_time == expected_time);
        REQUIRE(read_result->header.size() == header2.size());
        REQUIRE(std::memcmp(read_result->header.data(), header2.data(), header2.size()) == 0);
        REQUIRE(read_result->data.size() == data2.size());
        REQUIRE(std::memcmp(read_result->data.data(), data2.data(), data2.size()) == 0);
        REQUIRE_FALSE(read_result->is_repeated_persistent);
        REQUIRE_FALSE(read_result->is_lite_compressed);
        expected_time += message_interval;

        --expected_sequence_number;
        read_result = reader.read_next();
        REQUIRE(read_result);
        REQUIRE(read_result->channel_name == channel_name1);
        REQUIRE(read_result->sequence_number == expected_sequence_number);
        REQUIRE(read_result->log_time == expected_time + std::chrono::nanoseconds(1));
        REQUIRE(read_result->transmit_time == expected_time);
        REQUIRE(read_result->header.size() == header1.size());
        REQUIRE(std::memcmp(read_result->header.data(), header1.data(), header1.size()) == 0);
        REQUIRE(read_result->data.size() == data1.size());
        REQUIRE(std::memcmp(read_result->data.data(), data1.data(), data1.size()) == 0);
        REQUIRE_FALSE(read_result->is_repeated_persistent);
        REQUIRE_FALSE(read_result->is_lite_compressed);
        expected_time += message_interval;
      }

      REQUIRE_FALSE(reader);
      REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));
    }

    SECTION("Filter by channel name")
    {
      const std::pmr::unordered_set<std::pmr::string> desired_channels = {channel_name1};
      REQUIRE(reader.open(desired_channels));
      REQUIRE(reader);

      auto expected_sequence_number = message_count * 2U;
      auto expected_time = end_time;
      for (size_t i = 0U; i < message_count; ++i)
      {
        CAPTURE(i);

        --expected_sequence_number;
        expected_time += message_interval;

        --expected_sequence_number;
        auto read_result = reader.read_next();
        REQUIRE(read_result);
        REQUIRE(read_result->channel_name == channel_name1);
        REQUIRE(read_result->sequence_number == expected_sequence_number);
        REQUIRE(read_result->log_time == expected_time + std::chrono::nanoseconds(1));
        REQUIRE(read_result->transmit_time == expected_time);
        REQUIRE(read_result->header.size() == header1.size());
        REQUIRE(std::memcmp(read_result->header.data(), header1.data(), header1.size()) == 0);
        REQUIRE(read_result->data.size() == data1.size());
        REQUIRE(std::memcmp(read_result->data.data(), data1.data(), data1.size()) == 0);
        REQUIRE_FALSE(read_result->is_repeated_persistent);
        REQUIRE_FALSE(read_result->is_lite_compressed);
        expected_time += message_interval;
      }

      REQUIRE_FALSE(reader);
      REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));
    }

    SECTION("Filter by channel name and time range")
    {
      const std::pmr::unordered_set<std::pmr::string> desired_channels = {channel_name2};
      const LogInterval log_interval{end_time + message_interval * 1000, start_time - message_interval * 1000};
      REQUIRE(reader.open(desired_channels, log_interval));
      REQUIRE(reader);

      auto expected_sequence_number = message_count * 2U;
      auto expected_time = end_time;
      for (size_t i = 0U; i < message_count; ++i)
      {
        CAPTURE(i);

        --expected_sequence_number;
        if (log_interval.contains(expected_time))
        {
          auto read_result = reader.read_next();
          REQUIRE(read_result);
          REQUIRE(read_result->channel_name == channel_name2);
          REQUIRE(read_result->sequence_number == expected_sequence_number);
          REQUIRE(read_result->log_time == expected_time + std::chrono::nanoseconds(1));
          REQUIRE(read_result->transmit_time == expected_time);
          REQUIRE(read_result->header.size() == header2.size());
          REQUIRE(std::memcmp(read_result->header.data(), header2.data(), header2.size()) == 0);
          REQUIRE(read_result->data.size() == data2.size());
          REQUIRE_FALSE(read_result->is_repeated_persistent);
          REQUIRE_FALSE(read_result->is_lite_compressed);
          REQUIRE(std::memcmp(read_result->data.data(), data2.data(), data2.size()) == 0);
        }
        expected_time += message_interval;

        --expected_sequence_number;
        expected_time += message_interval;
      }

      REQUIRE_FALSE(reader);
      REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));
    }

    SECTION("No channels match desired channels")
    {
      const std::pmr::unordered_set<std::pmr::string> desired_channels = {"NOT A CHANNEL"};
      REQUIRE(reader.open(desired_channels));
      REQUIRE_FALSE(reader);
    }

    SECTION("Time range doesn't overlap log range")
    {
      const LogInterval log_interval{LogTimestamp{0}, LogTimestamp{1}};
      REQUIRE(reader.open({}, log_interval));
      REQUIRE_FALSE(reader);
    }
  }

  SECTION("Error handling")
  {
    SECTION("No log directory")
    {
      Reader reader{memory_resource, test_log_path.string()};
      REQUIRE_FALSE(reader);
      REQUIRE(reader.get_metadata() == jewels::unexpected(LogError::no_such_file_or_directory));
      REQUIRE(reader.get_channel_metadata("not_a_channel") == jewels::unexpected(LogError::no_such_file_or_directory));
      REQUIRE(reader.get_channels() == jewels::unexpected(LogError::no_such_file_or_directory));
      REQUIRE(reader.get_metrics() == jewels::unexpected(LogError::no_such_file_or_directory));
      REQUIRE(reader.open() == jewels::unexpected(LogError::no_such_file_or_directory));
      REQUIRE(reader.read_next() == jewels::unexpected(LogError::not_open));
    }

    SECTION("No log file")
    {
      REQUIRE(vfs.create_directories(test_log_path).has_value());
      Reader reader{memory_resource, test_log_path.string()};
      REQUIRE_FALSE(reader);
      REQUIRE(reader.get_metadata() == jewels::unexpected(LogError::not_a_log));
      REQUIRE(reader.get_channel_metadata("not_a_channel") == jewels::unexpected(LogError::not_a_log));
      REQUIRE(reader.get_channels() == jewels::unexpected(LogError::not_a_log));
      REQUIRE(reader.get_metrics() == jewels::unexpected(LogError::not_a_log));
      REQUIRE(reader.open() == jewels::unexpected(LogError::not_a_log));
      REQUIRE(reader.read_next() == jewels::unexpected(LogError::not_open));
    }
  }
}

TEST_CASE("Reader is deterministic")
{
  constexpr auto test_log_name = "test_log";

  const auto message_chunk_index_format = GENERATE(MessageChunkIndexFormat::v1, MessageChunkIndexFormat::v2);
  CAPTURE(message_chunk_index_format);
  const auto recover_metadata = GENERATE(false, true);
  CAPTURE(recover_metadata);

  const auto memory_resource = jewels::memory::get_default_memory_resource();
  const jewels::filesystem::Filesystem vfs{memory_resource};
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto test_log_path = test_dir.get_path() / test_log_name;
  Writer writer{memory_resource, message_chunk_index_format};

  constexpr auto channel_name1 = "channel1";
  constexpr auto metadata1 = LoggedChannelMetadata{
    .channel_name = channel_name1,
    .message_encoding = MessageEncoding::unspecified,
    .channel_type = ChannelType::regular,
    .schema_name = "schema1",
    .schema_encoding = SchemaEncoding::undefined,
    .schema_definition = "Schema definition 1",
  };
  constexpr auto header1_size = 123U;
  std::vector<std::byte> header1(header1_size);
  onboard::tests::fill_with_random_bytes(header1);
  constexpr auto data1_size = 1234U;
  std::vector<std::byte> data1(data1_size);
  onboard::tests::fill_with_random_bytes(data1);

  constexpr auto channel_name2 = "channel2";
  constexpr auto metadata2 = LoggedChannelMetadata{
    .channel_name = channel_name2,
    .message_encoding = MessageEncoding::unspecified,
    .channel_type = ChannelType::persistent,
    .schema_name = "schema2",
    .schema_encoding = SchemaEncoding::undefined,
    .schema_definition = "Schema definition 2",
  };
  constexpr auto header2_size = 234U;
  std::vector<std::byte> header2(header2_size);
  onboard::tests::fill_with_random_bytes(header2);
  constexpr auto data2_size = 2345U;
  std::vector<std::byte> data2(data2_size);
  onboard::tests::fill_with_random_bytes(data2);

  constexpr auto channel_name3 = "channel3";
  constexpr auto metadata3 = LoggedChannelMetadata{
    .channel_name = channel_name3,
    .message_encoding = MessageEncoding::unspecified,
    .channel_type = ChannelType::persistent,
    .schema_name = "schema3",
    .schema_encoding = SchemaEncoding::undefined,
    .schema_definition = "Schema definition 3",
  };
  constexpr auto header3_size = 345U;
  std::vector<std::byte> header3(header3_size);
  onboard::tests::fill_with_random_bytes(header3);
  constexpr auto data3_size = 3456U;
  std::vector<std::byte> data3(data3_size);
  onboard::tests::fill_with_random_bytes(data3);

  constexpr auto channel_name4 = "channel4";
  constexpr auto metadata4 = LoggedChannelMetadata{
    .channel_name = channel_name4,
    .message_encoding = MessageEncoding::unspecified,
    .channel_type = ChannelType::persistent,
    .schema_name = "schema4",
    .schema_encoding = SchemaEncoding::undefined,
    .schema_definition = "Schema definition 4",
  };
  constexpr auto header4_size = 456U;
  std::vector<std::byte> header4(header4_size);
  onboard::tests::fill_with_random_bytes(header4);
  constexpr auto data4_size = 4567U;
  std::vector<std::byte> data4(data4_size);
  onboard::tests::fill_with_random_bytes(data4);

  constexpr auto channel_name5 = "channel5";
  constexpr auto metadata5 = LoggedChannelMetadata{
    .channel_name = channel_name5,
    .message_encoding = MessageEncoding::unspecified,
    .channel_type = ChannelType::persistent,
    .schema_name = "schema5",
    .schema_encoding = SchemaEncoding::undefined,
    .schema_definition = "Schema definition 5",
  };

  REQUIRE(writer.open(test_log_path.string()));
  REQUIRE(writer.create_channel(metadata1));
  REQUIRE(writer.create_channel(metadata2));
  REQUIRE(writer.create_channel(metadata3));
  REQUIRE(writer.create_channel(metadata4));
  REQUIRE(writer.create_channel(metadata5));

  const uint32_t message_count = 2U;
  const LogTimestamp start_time{std::chrono::seconds{1'000'000}};
  const std::chrono::seconds message_interval{1};
  auto transmit_time = start_time;
  for (uint32_t i = 0U; i < message_count; ++i)
  {
    REQUIRE(writer.write(
      LoggedMessage{
        .channel_name = channel_name4,
        .sequence_number = message_count - i - 1,
        .log_time = transmit_time + std::chrono::nanoseconds(1),
        .transmit_time = transmit_time,
        .header = header4,
        .data = data4,
        .is_repeated_persistent = false,
        .is_lite_compressed = false,
      }));

    REQUIRE(writer.write(
      LoggedMessage{
        .channel_name = channel_name2,
        .sequence_number = message_count - i - 1,
        .log_time = transmit_time + std::chrono::nanoseconds(1),
        .transmit_time = transmit_time,
        .header = header2,
        .data = data2,
        .is_repeated_persistent = false,
        .is_lite_compressed = false,
      }));

    REQUIRE(writer.write(
      LoggedMessage{
        .channel_name = channel_name3,
        .sequence_number = message_count - i - 1,
        .log_time = transmit_time + std::chrono::nanoseconds(1),
        .transmit_time = transmit_time,
        .header = header3,
        .data = data3,
        .is_repeated_persistent = false,
        .is_lite_compressed = false,
      }));

    REQUIRE(writer.write(
      LoggedMessage{
        .channel_name = channel_name1,
        .sequence_number = message_count - i - 1,
        .log_time = transmit_time + std::chrono::nanoseconds(1),
        .transmit_time = transmit_time,
        .header = header1,
        .data = data1,
        .is_repeated_persistent = false,
        .is_lite_compressed = false,
      }));

    transmit_time -= message_interval;
  }
  const auto end_time = transmit_time + message_interval;

  REQUIRE(writer.close());

  if (recover_metadata)
  {
    REQUIRE(vfs.remove(test_log_path / "stack_log_metadata.pbtxt").has_value());
  }

  Reader reader{memory_resource, test_log_path.string()};
  REQUIRE(reader.open());

  auto expected_time = end_time;
  for (size_t i = 0U; i < message_count; ++i)
  {
    CAPTURE(i);

    auto read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name1);
    REQUIRE(read_result->sequence_number == i);
    REQUIRE(read_result->log_time == expected_time + std::chrono::nanoseconds(1));
    REQUIRE(read_result->transmit_time == expected_time);
    REQUIRE(read_result->header.size() == header1.size());
    REQUIRE(std::memcmp(read_result->header.data(), header1.data(), header1.size()) == 0);
    REQUIRE(read_result->data.size() == data1.size());
    REQUIRE(std::memcmp(read_result->data.data(), data1.data(), data1.size()) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);
    REQUIRE_FALSE(read_result->is_lite_compressed);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);
    REQUIRE(read_result->sequence_number == i);
    REQUIRE(read_result->log_time == expected_time + std::chrono::nanoseconds(1));
    REQUIRE(read_result->transmit_time == expected_time);
    REQUIRE(read_result->header.size() == header2.size());
    REQUIRE(std::memcmp(read_result->header.data(), header2.data(), header2.size()) == 0);
    REQUIRE(read_result->data.size() == data2.size());
    REQUIRE(std::memcmp(read_result->data.data(), data2.data(), data2.size()) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);
    REQUIRE_FALSE(read_result->is_lite_compressed);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name3);
    REQUIRE(read_result->sequence_number == i);
    REQUIRE(read_result->log_time == expected_time + std::chrono::nanoseconds(1));
    REQUIRE(read_result->transmit_time == expected_time);
    REQUIRE(read_result->header.size() == header3.size());
    REQUIRE(std::memcmp(read_result->header.data(), header3.data(), header3.size()) == 0);
    REQUIRE(read_result->data.size() == data3.size());
    REQUIRE(std::memcmp(read_result->data.data(), data3.data(), data3.size()) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);
    REQUIRE_FALSE(read_result->is_lite_compressed);

    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name4);
    REQUIRE(read_result->sequence_number == i);
    REQUIRE(read_result->log_time == expected_time + std::chrono::nanoseconds(1));
    REQUIRE(read_result->transmit_time == expected_time);
    REQUIRE(read_result->header.size() == header4.size());
    REQUIRE(std::memcmp(read_result->header.data(), header4.data(), header4.size()) == 0);
    REQUIRE(read_result->data.size() == data4.size());
    REQUIRE(std::memcmp(read_result->data.data(), data4.data(), data4.size()) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);
    REQUIRE_FALSE(read_result->is_lite_compressed);

    expected_time += message_interval;
  }

  REQUIRE_FALSE(reader);
  REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));
}

TEST_CASE("Duplicate messages are filtered")
{
  constexpr auto test_log_name = "test_log";

  // Configure writer to put each channel in a separate file
  constexpr auto writer_config_text = R"(
    # proto-file: clockwork/logging/offboard/v1/writer_config.proto
    # proto-message: WriterConfig
    rule {
      regex: ".*"
    }
  )";

  const auto message_chunk_index_format = GENERATE(MessageChunkIndexFormat::v1, MessageChunkIndexFormat::v2);
  CAPTURE(message_chunk_index_format);
  const auto recover_metadata = GENERATE(false, true);
  CAPTURE(recover_metadata);

  const auto memory_resource = jewels::memory::get_default_memory_resource();
  const jewels::filesystem::Filesystem vfs{memory_resource};
  LiteCompressor lite_compressor{memory_resource};
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto test_log_path = test_dir.get_path() / test_log_name;
  Writer writer{memory_resource, message_chunk_index_format};

  constexpr auto channel_name1 = "channel1";
  constexpr auto metadata1 = LoggedChannelMetadata{
    .channel_name = channel_name1,
    .message_encoding = MessageEncoding::unspecified,
    .channel_type = ChannelType::regular,
    .schema_name = "schema1",
    .schema_encoding = SchemaEncoding::undefined,
    .schema_definition = "Schema definition 1",
  };
  constexpr auto header1_size = 123U;
  std::vector<std::byte> header1(header1_size);
  onboard::tests::fill_with_random_bytes(header1);
  constexpr auto data1_size = 1234U;
  std::vector<std::byte> data1(data1_size);
  onboard::tests::fill_with_random_bytes(data1);

  constexpr auto channel_name2 = "channel2";
  constexpr auto metadata2 = LoggedChannelMetadata{
    .channel_name = channel_name2,
    .message_encoding = MessageEncoding::unspecified,
    .channel_type = ChannelType::persistent,
    .schema_name = "schema2",
    .schema_encoding = SchemaEncoding::undefined,
    .schema_definition = "Schema definition 2",
  };
  constexpr auto header2_size = 234U;
  std::vector<std::byte> header2(header2_size);
  onboard::tests::fill_with_random_bytes(header2);
  constexpr auto data2_size = 2345U;
  std::vector<std::byte> data2(data2_size);
  onboard::tests::fill_with_random_bytes(data2);
  const auto compressed_data2 = offboard::tests::lite_compress(data2, lite_compressor);

  REQUIRE(writer.open(test_log_path.string(), writer_config_text));
  REQUIRE(writer.create_channel(metadata1));
  REQUIRE(writer.create_channel(metadata2));

  const uint32_t message_count = 20000U;
  const LogTimestamp start_time{std::chrono::seconds{1'000'000}};
  const std::chrono::seconds message_interval{1};
  auto transmit_time = start_time;
  for (uint32_t i = 0U; i < message_count; ++i)
  {
    REQUIRE(writer.write(
      LoggedMessage{
        .channel_name = channel_name1,
        .sequence_number = i * 2U,
        .log_time = transmit_time + std::chrono::nanoseconds(1),
        .transmit_time = transmit_time,
        .header = header1,
        .data = data1,
        .is_repeated_persistent = false,
        .is_lite_compressed = false,
      }));
    transmit_time -= message_interval;

    REQUIRE(writer.write(
      LoggedMessage{
        .channel_name = channel_name2,
        .sequence_number = (i * 2U) + 1U,
        .log_time = transmit_time + std::chrono::nanoseconds(1),
        .transmit_time = transmit_time,
        .header = header2,
        .data = compressed_data2,
        .is_repeated_persistent = false,
        .is_lite_compressed = true,
      }));
    transmit_time -= message_interval;
  }
  transmit_time = start_time;
  for (uint32_t i = 0U; i < message_count; ++i)
  {
    REQUIRE(writer.write(
      LoggedMessage{
        .channel_name = channel_name1,
        .sequence_number = i * 2U,
        .log_time = transmit_time + std::chrono::nanoseconds(1),
        .transmit_time = transmit_time,
        .header = header1,
        .data = data1,
        .is_repeated_persistent = false,
        .is_lite_compressed = false,
      }));
    transmit_time -= message_interval;

    REQUIRE(writer.write(
      LoggedMessage{
        .channel_name = channel_name2,
        .sequence_number = (i * 2U) + 1U,
        .log_time = transmit_time + std::chrono::nanoseconds(1),
        .transmit_time = transmit_time,
        .header = header2,
        .data = compressed_data2,
        .is_repeated_persistent = false,
        .is_lite_compressed = true,
      }));
    transmit_time -= message_interval;
  }
  const auto end_time = transmit_time + message_interval;

  REQUIRE(writer.close());

  if (recover_metadata)
  {
    REQUIRE(vfs.remove(test_log_path / "stack_log_metadata.pbtxt").has_value());
  }

  Reader reader{memory_resource, test_log_path.string()};

  REQUIRE(reader.open());
  REQUIRE(reader);

  auto expected_sequence_number = message_count * 2U;
  auto expected_time = end_time;
  for (size_t i = 0U; i < message_count; ++i)
  {
    CAPTURE(i);

    --expected_sequence_number;
    auto read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name2);
    REQUIRE(read_result->sequence_number == expected_sequence_number);
    REQUIRE(read_result->log_time == expected_time + std::chrono::nanoseconds(1));
    REQUIRE(read_result->transmit_time == expected_time);
    REQUIRE(read_result->header.size() == header2.size());
    REQUIRE(std::memcmp(read_result->header.data(), header2.data(), header2.size()) == 0);
    REQUIRE(read_result->data.size() == data2.size());
    REQUIRE(std::memcmp(read_result->data.data(), data2.data(), data2.size()) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);
    REQUIRE_FALSE(read_result->is_lite_compressed);
    expected_time += message_interval;

    --expected_sequence_number;
    read_result = reader.read_next();
    REQUIRE(read_result);
    REQUIRE(read_result->channel_name == channel_name1);
    REQUIRE(read_result->sequence_number == expected_sequence_number);
    REQUIRE(read_result->log_time == expected_time + std::chrono::nanoseconds(1));
    REQUIRE(read_result->transmit_time == expected_time);
    REQUIRE(read_result->header.size() == header1.size());
    REQUIRE(std::memcmp(read_result->header.data(), header1.data(), header1.size()) == 0);
    REQUIRE(read_result->data.size() == data1.size());
    REQUIRE(std::memcmp(read_result->data.data(), data1.data(), data1.size()) == 0);
    REQUIRE_FALSE(read_result->is_repeated_persistent);
    REQUIRE_FALSE(read_result->is_lite_compressed);
    expected_time += message_interval;
  }

  REQUIRE_FALSE(reader.read_next());
  REQUIRE_FALSE(reader);
  REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));
}

} // namespace
} // namespace clockwork_logging::offboard
