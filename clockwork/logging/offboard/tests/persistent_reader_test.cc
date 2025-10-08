// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/channel_type.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/message_encoding.hh"
#include "clockwork/logging/offboard/reader.hh"
#include "clockwork/logging/offboard/types.hh"
#include "clockwork/logging/offboard/writer.hh"
#include "clockwork/logging/onboard/tests/support/test_support.hh"
#include "clockwork/logging/schema_encoding.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/tmp_directory_guard.hh"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
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

TEST_CASE("Log with persistent channels")
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

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto test_log_path = test_dir.get_path() / test_log_name;
  Writer writer{memory_resource};

  constexpr auto channel_name1 = "channel1";
  constexpr auto metadata1 = LoggedChannelMetadata{
    .channel_name = channel_name1,
    .message_encoding = MessageEncoding::unspecified,
    .channel_type = ChannelType::persistent,
    .schema_name = "schema1",
    .schema_encoding = SchemaEncoding::unspecified,
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
    .schema_encoding = SchemaEncoding::unspecified,
    .schema_definition = "Schema definition 2",
  };
  constexpr auto header2_size = 234U;
  std::vector<std::byte> header2(header2_size);
  onboard::tests::fill_with_random_bytes(header2);
  constexpr auto data2_size = 2345U;
  std::vector<std::byte> data2(data2_size);
  onboard::tests::fill_with_random_bytes(data2);

  REQUIRE(writer.open(test_log_path.string(), writer_config_text));
  REQUIRE(writer.create_channel(metadata1));
  REQUIRE(writer.create_channel(metadata2));

  constexpr uint32_t num_log_files = 3U;
  constexpr uint32_t messages_per_file = 3U;
  constexpr auto message_count = num_log_files * messages_per_file;

  const LogTimestamp start_time{std::chrono::seconds{1'000'000}};
  const std::chrono::seconds message_interval{1};
  auto transmit_time = start_time;
  for (uint32_t i = 0U; i < message_count; ++i)
  {
    if (((i % messages_per_file) == 0U) && (i != 0U))
    {
      REQUIRE(writer.split_log_files());
    }

    REQUIRE(writer.write(
      LoggedMessage{
        .channel_name = channel_name1,
        .sequence_number = i * 2U,
        .log_time = transmit_time + std::chrono::nanoseconds(1),
        .transmit_time = transmit_time,
        .header = header1,
        .data = data1,
        .is_repeated_persistent = false,
      }));
    transmit_time -= message_interval;

    REQUIRE(writer.write(
      LoggedMessage{
        .channel_name = channel_name2,
        .sequence_number = (i * 2U) + 1U,
        .log_time = transmit_time + std::chrono::nanoseconds(1),
        .transmit_time = transmit_time,
        .header = header2,
        .data = data2,
        .is_repeated_persistent = false,
      }));
    transmit_time -= message_interval;
  }
  const auto end_time = transmit_time + message_interval;
  REQUIRE(writer.close());

  Reader reader{memory_resource, test_log_path.string()};

  SECTION("Check metadata")
  {
    const auto metadata_result = reader.get_metadata();
    REQUIRE(metadata_result);
    REQUIRE((*metadata_result)->size() == 2U);
    REQUIRE((*metadata_result)->at(channel_name1) == metadata1);
    REQUIRE((*metadata_result)->at(channel_name2) == metadata2);

    const auto channels_result = reader.get_channels();
    REQUIRE(channels_result);
    REQUIRE((*channels_result)->size() == 2U);
    REQUIRE((*channels_result)->contains(channel_name1));
    REQUIRE((*channels_result)->contains(channel_name2));

    const auto metrics_result = reader.get_metrics();
    REQUIRE(metrics_result);
    REQUIRE((*metrics_result)->message_count == message_count * 2U);
    REQUIRE(
      (*metrics_result)->byte_count == message_count * (data1.size() + data2.size() + header1.size() + header2.size()));
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
      (*metrics_result)->metrics_map.at(channel_name2).byte_count == message_count * (data2.size() + header2.size()));
    REQUIRE(
      (*metrics_result)->metrics_map.at(channel_name2).transmit_time_interval ==
      LogInterval{start_time - message_interval, end_time});
  }

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
      expected_time += message_interval;
    }

    REQUIRE_FALSE(reader);
    REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));
  }

  SECTION("Channel2 only, filter by exact time range")
  {
    const auto first_message_index = GENERATE_REF(range(0U, message_count - 1U));
    CAPTURE(first_message_index);

    const std::pmr::unordered_set<std::pmr::string> desired_channels = {channel_name2};
    const LogInterval log_interval{
      end_time + (message_interval * static_cast<int64_t>(first_message_index) * 2), start_time};
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
        REQUIRE(std::memcmp(read_result->data.data(), data2.data(), data2.size()) == 0);
        REQUIRE_FALSE(read_result->is_repeated_persistent);
      }
      expected_time += message_interval;

      --expected_sequence_number;
      expected_time += message_interval;
    }

    REQUIRE_FALSE(reader);
    REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));
    reader.close();
  }

  SECTION("Channel 2 only, Interval starts after a persistent message")
  {
    const auto first_message_index = GENERATE_REF(range(0U, message_count - 1U));
    CAPTURE(first_message_index);

    const std::pmr::unordered_set<std::pmr::string> desired_channels = {channel_name2};
    const LogInterval log_interval{
      end_time + (message_interval * static_cast<int64_t>(first_message_index) * 2) + std::chrono::nanoseconds(1),
      start_time};
    REQUIRE(reader.open(desired_channels, log_interval));
    REQUIRE(reader);

    auto expected_sequence_number = message_count * 2U;
    auto expected_time = end_time;
    for (size_t i = 0U; i < message_count; ++i)
    {
      CAPTURE(i);

      --expected_sequence_number;
      if (log_interval.contains(expected_time) || (i == first_message_index))
      {
        auto read_result = reader.read_next();
        REQUIRE(read_result);
        REQUIRE(read_result->channel_name == channel_name2);
        REQUIRE(read_result->sequence_number == expected_sequence_number);
        REQUIRE(read_result->log_time == expected_time + std::chrono::nanoseconds(1));
        if (i == first_message_index)
        {
          REQUIRE(read_result->transmit_time == log_interval.get_start_timestamp());
        }
        else
        {
          REQUIRE(read_result->transmit_time == expected_time);
        }
        REQUIRE(read_result->header.size() == header2.size());
        REQUIRE(std::memcmp(read_result->header.data(), header2.data(), header2.size()) == 0);
        REQUIRE(read_result->data.size() == data2.size());
        REQUIRE(std::memcmp(read_result->data.data(), data2.data(), data2.size()) == 0);
        REQUIRE(read_result->is_repeated_persistent == (i == first_message_index));
      }
      expected_time += message_interval;

      --expected_sequence_number;
      expected_time += message_interval;
    }

    REQUIRE_FALSE(reader);
    REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));
    reader.close();
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

TEST_CASE("Log with repeated persistent channels")
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

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto test_log_path = test_dir.get_path() / test_log_name;
  Writer writer{memory_resource};

  constexpr auto channel_name1 = "channel1";
  constexpr auto metadata1 = LoggedChannelMetadata{
    .channel_name = channel_name1,
    .message_encoding = MessageEncoding::unspecified,
    .channel_type = ChannelType::persistent,
    .schema_name = "schema1",
    .schema_encoding = SchemaEncoding::unspecified,
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
    .schema_encoding = SchemaEncoding::unspecified,
    .schema_definition = "Schema definition 2",
  };
  constexpr auto header2_size = 234U;
  std::vector<std::byte> header2(header2_size);
  onboard::tests::fill_with_random_bytes(header2);
  constexpr auto data2_size = 2345U;
  std::vector<std::byte> data2(data2_size);
  onboard::tests::fill_with_random_bytes(data2);

  REQUIRE(writer.open(test_log_path.string(), writer_config_text));
  REQUIRE(writer.create_channel(metadata1));
  REQUIRE(writer.create_channel(metadata2));

  constexpr uint32_t num_log_files = 3U;
  constexpr uint32_t messages_per_file = 3U;
  constexpr auto message_count = num_log_files * messages_per_file;

  const LogTimestamp start_time{std::chrono::seconds{1'000'000}};
  const std::chrono::seconds message_interval{1};
  auto transmit_time = start_time;
  LoggedMessage prev_channel2_message{};
  for (uint32_t i = 0U; i < message_count; ++i)
  {
    if (((i % messages_per_file) == 0U) && (i != 0U))
    {
      REQUIRE(writer.split_log_files());
      prev_channel2_message.transmit_time += std::chrono::milliseconds(500);
      prev_channel2_message.is_repeated_persistent = true;
      REQUIRE(writer.write(prev_channel2_message));
    }

    REQUIRE(writer.write(
      LoggedMessage{
        .channel_name = channel_name1,
        .sequence_number = i * 2U,
        .log_time = transmit_time + std::chrono::nanoseconds(1),
        .transmit_time = transmit_time,
        .header = header1,
        .data = data1,
        .is_repeated_persistent = false,
      }));
    transmit_time += message_interval;

    prev_channel2_message = LoggedMessage{
      .channel_name = channel_name2,
      .sequence_number = (i * 2U) + 1U,
      .log_time = transmit_time + std::chrono::nanoseconds(1),
      .transmit_time = transmit_time,
      .header = header2,
      .data = data2,
      .is_repeated_persistent = false,
    };
    REQUIRE(writer.write(prev_channel2_message));
    transmit_time += message_interval;
  }
  const auto end_time = transmit_time - message_interval;
  REQUIRE(writer.close());

  Reader reader{memory_resource, test_log_path.string()};

  SECTION("Check metadata")
  {
    const auto metadata_result = reader.get_metadata();
    REQUIRE(metadata_result);
    REQUIRE((*metadata_result)->size() == 2U);
    REQUIRE((*metadata_result)->at(channel_name1) == metadata1);
    REQUIRE((*metadata_result)->at(channel_name2) == metadata2);

    const auto channels_result = reader.get_channels();
    REQUIRE(channels_result);
    REQUIRE((*channels_result)->size() == 2U);
    REQUIRE((*channels_result)->contains(channel_name1));
    REQUIRE((*channels_result)->contains(channel_name2));

    const auto metrics_result = reader.get_metrics();
    REQUIRE(metrics_result);
    REQUIRE((*metrics_result)->message_count == (message_count * 2U) + num_log_files - 1U);
    REQUIRE(
      (*metrics_result)->byte_count == (message_count * (data1.size() + header1.size())) +
                                         ((message_count + num_log_files - 1U) * (data2.size() + header2.size())));
    REQUIRE((*metrics_result)->transmit_time_interval == LogInterval{start_time, end_time});
    REQUIRE((*metrics_result)->metrics_map.size() == 2U);
    REQUIRE((*metrics_result)->metrics_map.at(channel_name1).message_count == message_count);
    REQUIRE(
      (*metrics_result)->metrics_map.at(channel_name1).byte_count == message_count * (data1.size() + header1.size()));
    REQUIRE(
      (*metrics_result)->metrics_map.at(channel_name1).transmit_time_interval ==
      LogInterval{start_time, end_time - message_interval});
    REQUIRE((*metrics_result)->metrics_map.at(channel_name2).message_count == message_count + num_log_files - 1U);
    REQUIRE(
      (*metrics_result)->metrics_map.at(channel_name2).byte_count ==
      (message_count + num_log_files - 1U) * (data2.size() + header2.size()));
    REQUIRE(
      (*metrics_result)->metrics_map.at(channel_name2).transmit_time_interval ==
      LogInterval{start_time + message_interval, end_time});
  }

  SECTION("Read entire log")
  {
    REQUIRE(reader.open());
    REQUIRE(reader);

    auto expected_sequence_number = 0U;
    auto expected_time = start_time;
    for (size_t i = 0U; i < message_count; ++i)
    {
      CAPTURE(i);

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
      ++expected_sequence_number;
      expected_time += message_interval;

      read_result = reader.read_next();
      REQUIRE(read_result);
      REQUIRE(read_result->channel_name == channel_name2);
      REQUIRE(read_result->sequence_number == expected_sequence_number);
      REQUIRE(read_result->log_time == expected_time + std::chrono::nanoseconds(1));
      REQUIRE(read_result->transmit_time == expected_time);
      REQUIRE(read_result->header.size() == header2.size());
      REQUIRE(std::memcmp(read_result->header.data(), header2.data(), header2.size()) == 0);
      REQUIRE(read_result->data.size() == data2.size());
      REQUIRE_FALSE(read_result->is_repeated_persistent);
      REQUIRE(std::memcmp(read_result->data.data(), data2.data(), data2.size()) == 0);
      ++expected_sequence_number;
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

    auto expected_sequence_number = 0U;
    auto expected_time = start_time;
    for (size_t i = 0U; i < message_count; ++i)
    {
      CAPTURE(i);

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
      ++expected_sequence_number;
      expected_time += message_interval;

      ++expected_sequence_number;
      expected_time += message_interval;
    }

    REQUIRE_FALSE(reader);
    REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));
  }

  SECTION("Channel2 only, filter by exact time range")
  {
    const auto first_message_index = GENERATE_REF(range(0U, message_count - 1U));
    CAPTURE(first_message_index);

    const std::pmr::unordered_set<std::pmr::string> desired_channels = {channel_name2};
    const LogInterval log_interval{
      start_time + (message_interval * ((static_cast<int64_t>(first_message_index) * 2) + 1)), end_time};
    REQUIRE(reader.open(desired_channels, log_interval));
    REQUIRE(reader);

    auto expected_sequence_number = 0U;
    auto expected_time = start_time;
    for (size_t i = 0U; i < message_count; ++i)
    {
      CAPTURE(i);

      ++expected_sequence_number;
      expected_time += message_interval;

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
        REQUIRE(std::memcmp(read_result->data.data(), data2.data(), data2.size()) == 0);
        REQUIRE_FALSE(read_result->is_repeated_persistent);
      }
      ++expected_sequence_number;
      expected_time += message_interval;
    }

    REQUIRE_FALSE(reader);
    REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));
    reader.close();
  }

  SECTION("Channel 2 only, Interval starts after a persistent message")
  {
    const auto first_message_index = GENERATE_REF(range(0U, message_count - 1U));
    CAPTURE(first_message_index);
    const auto interval_offset_ms = GENERATE(1, 500, 501, 1999);
    CAPTURE(interval_offset_ms);

    const std::pmr::unordered_set<std::pmr::string> desired_channels = {channel_name2};
    const LogInterval log_interval{
      start_time + (message_interval * ((static_cast<int64_t>(first_message_index) * 2) + 1)) +
        std::chrono::milliseconds(interval_offset_ms),
      end_time};
    REQUIRE(reader.open(desired_channels, log_interval));
    REQUIRE(reader);

    auto expected_sequence_number = 0U;
    auto expected_time = start_time;
    for (size_t i = 0U; i < message_count; ++i)
    {
      CAPTURE(i);

      ++expected_sequence_number;
      expected_time += message_interval;

      if (log_interval.contains(expected_time) || (i == first_message_index))
      {
        auto read_result = reader.read_next();
        REQUIRE(read_result);
        CHECK(read_result->channel_name == channel_name2);
        CHECK(read_result->sequence_number == expected_sequence_number);
        CHECK(read_result->log_time == expected_time + std::chrono::nanoseconds(1));
        if (i == first_message_index)
        {
          REQUIRE(read_result->transmit_time == log_interval.get_start_timestamp());
        }
        else
        {
          REQUIRE(read_result->transmit_time == expected_time);
        }
        REQUIRE(read_result->header.size() == header2.size());
        REQUIRE(std::memcmp(read_result->header.data(), header2.data(), header2.size()) == 0);
        REQUIRE(read_result->data.size() == data2.size());
        REQUIRE(std::memcmp(read_result->data.data(), data2.data(), data2.size()) == 0);
        REQUIRE(read_result->is_repeated_persistent == (i == first_message_index));
      }
      ++expected_sequence_number;
      expected_time += message_interval;
    }

    REQUIRE_FALSE(reader);
    REQUIRE(reader.read_next() == jewels::unexpected(LogError::end_of_log));
    reader.close();
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

} // namespace
} // namespace clockwork_logging::offboard
