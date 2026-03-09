// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/channel_type_clk_cc.hh"
#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/decompress_option.hh"
#include "clockwork/logging/lite_compressor.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/message_encoding_clk_cc.hh"
#include "clockwork/logging/offboard/tests/support/test_support.hh"
#include "clockwork/logging/onboard/null_message_handle.hh"
#include "clockwork/logging/onboard/types.hh"
#include "clockwork/logging/onboard/writer.hh"
#include "clockwork/logging/onboard/writer_policy.hh"
#include "clockwork/logging/readers/onboard_log_reader.hh"
#include "clockwork/logging/readers/serialization.hh"
#include "clockwork/logging/readers/types.hh"
#include "clockwork/logging/schema_encoding_clk_cc.hh"
#include "clockwork/logging/tests/support/test_message_clk_cc.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/shared_pool/ref_counted_pool.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/time/sync_time.hh"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <fmt/format.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace clockwork_logging
{
namespace
{

using TestWriterPolicy = onboard::WriterPolicy<onboard::NullMessageHandle>;
using MsgType = clockwork::Tappy<tests::TestMessage>;

TEST_CASE("Onboard Log Reader")
{
  const auto decompress_option = GENERATE(DecompressOption::decompress, DecompressOption::dont_decompress);
  CAPTURE(decompress_option);

  constexpr size_t max_write_mib_per_sec = 100U;
  constexpr auto max_log_file_duration = std::chrono::seconds{0};
  const jewels::testing::TmpDirectoryGuard test_dir;

  // Write a simple test log.

  const auto log_path = (test_dir.get_path() / "onboard").string();

  auto topics = std::vector<std::string>({"/topic1", "/topic2"});
  auto msgs = std::map<std::string, std::vector<MsgType>>();
  auto is_repeated_persistent_flags = std::map<std::string, std::vector<bool>>();

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  LiteCompressor lite_compressor{memory_resource};

  {
    onboard::Writer<TestWriterPolicy> writer{
      memory_resource,
      memory_resource,
      max_write_mib_per_sec,
      max_log_file_duration,
      onboard::WriterEnvironment::normal};
    REQUIRE(writer.open_log(log_path, "onboard", jewels::time::SteadyClock::now()));

    REQUIRE(writer.add_channel(
      onboard::LoggedChannelMetadata{
        .channel_name = topics.at(0U),
        .compression_type = CompressionType::none,
        .message_encoding = static_cast<MessageEncoding>(clockwork::LoggingTraits<MsgType>::message_encoding),
        .channel_type = ChannelType::regular,
        .schema_name = clockwork::LoggingTraits<MsgType>::schema_name,
        .schema_encoding = static_cast<SchemaEncoding>(clockwork::LoggingTraits<MsgType>::schema_encoding),
        .schema_definition =
          std::string_view{
            clockwork::LoggingTraits<MsgType>::schema_definition.data(),
            clockwork::LoggingTraits<MsgType>::schema_definition.size()},
      },
      jewels::time::SteadyClock::now()));

    REQUIRE(writer.add_channel(
      onboard::LoggedChannelMetadata{
        .channel_name = topics.at(1U),
        .compression_type = CompressionType::none,
        .message_encoding = static_cast<MessageEncoding>(clockwork::LoggingTraits<MsgType>::message_encoding),
        .channel_type = ChannelType::persistent,
        .schema_name = clockwork::LoggingTraits<MsgType>::schema_name,
        .schema_encoding = static_cast<SchemaEncoding>(clockwork::LoggingTraits<MsgType>::schema_encoding),
        .schema_definition =
          std::string_view{
            clockwork::LoggingTraits<MsgType>::schema_definition.data(),
            clockwork::LoggingTraits<MsgType>::schema_definition.size()},
      },
      jewels::time::SteadyClock::now()));

    for (uint32_t i = 0U; i < 10U; ++i)
    {
      for (uint32_t j = 0U; j < topics.size(); ++j)
      {
        MsgType msg{};
        msg.get_underlying_message_string().set_truncate(fmt::format("Test{}", i));

        auto data = std::as_bytes(std::span{&msg, 1U});
        std::vector<std::byte> compressed_data;
        if ((i % 2U) == 0U)
        {
          compressed_data = offboard::tests::lite_compress(data, lite_compressor);
          data = compressed_data;
        }

        REQUIRE(writer.log_message(
          onboard::Message{
            .channel_name = topics.at(j),
            .sequence_number = i,
            .log_time = LogTimestamp(static_cast<int64_t>((i * 2U) + j)),
            .message_time = LogTimestamp(static_cast<int64_t>((i * 2U) + j)),
            .header = {},
            .data = data,
          },
          (i % 2U) == 0U,
          jewels::time::SteadyClock::now()));
        msgs[topics.at(j)].push_back(msg);
        is_repeated_persistent_flags[topics.at(j)].push_back(false);
      }
    }
    REQUIRE(writer.close_log(jewels::time::SteadyClock::now()));
    REQUIRE(writer.drain_async_operations());
  }

  SECTION("Topics")
  {
    OnboardLogReader reader(log_path, {}, {}, decompress_option);

    auto expected = std::vector<TopicMetadata>({
      {
        .name = "/topic1",
        .type = std::string{clockwork::LoggingTraits<MsgType>::schema_name},
        .message_encoding = static_cast<MessageEncoding>(clockwork::LoggingTraits<MsgType>::message_encoding),
        .channel_type = ChannelType::regular,
        .schema_encoding = static_cast<SchemaEncoding>(clockwork::LoggingTraits<MsgType>::schema_encoding),
        .schema_definition =
          std::string{
            clockwork::LoggingTraits<MsgType>::schema_definition.data(),
            clockwork::LoggingTraits<MsgType>::schema_definition.size()},
      },
      {
        .name = "/topic2",
        .type = std::string{clockwork::LoggingTraits<MsgType>::schema_name},
        .message_encoding = static_cast<MessageEncoding>(clockwork::LoggingTraits<MsgType>::message_encoding),
        .channel_type = ChannelType::persistent,
        .schema_encoding = static_cast<SchemaEncoding>(clockwork::LoggingTraits<MsgType>::schema_encoding),
        .schema_definition =
          std::string{
            clockwork::LoggingTraits<MsgType>::schema_definition.data(),
            clockwork::LoggingTraits<MsgType>::schema_definition.size()},

      },
    });

    auto actual = reader.get_metadata();
    REQUIRE(expected == actual);

    const auto channel1_metadata = reader.get_channel_metadata("/topic1");
    REQUIRE(channel1_metadata);
    REQUIRE(channel1_metadata == expected.front());

    const auto channel2_metadata = reader.get_channel_metadata("/topic2");
    REQUIRE(channel2_metadata);
    REQUIRE(channel2_metadata == expected.back());

    REQUIRE(reader.get_channels() == std::vector<std::string>{"/topic1", "/topic2"});
  }

  SECTION("Read all messages")
  {
    OnboardLogReader reader(log_path, {}, {}, decompress_option);
    REQUIRE(reader.open({}));

    auto actual_msgs = std::map<std::string, std::vector<MsgType>>();
    auto actual_is_repeated_persistent_flags = std::map<std::string, std::vector<bool>>();
    while (auto msg_result = reader.next_message())
    {
      auto msg = msg_result.value();
      if (msg.is_lite_compressed)
      {
        const auto decompress_result = lite_compressor.decompress(msg.data);
        REQUIRE(decompress_result);
        msg.data = decompress_result.value();
        msg.is_lite_compressed = false;
      }
      REQUIRE(
        msg.message_encoding == static_cast<MessageEncoding>(clockwork::LoggingTraits<MsgType>::message_encoding));
      deserialize_tachyon(actual_msgs[std::string(msg.topic)].emplace_back(), msg.data);
      actual_is_repeated_persistent_flags[std::string(msg.topic)].emplace_back(msg.is_repeated_persistent);
    }

    REQUIRE(msgs.size() == actual_msgs.size());
    REQUIRE(msgs.at(topics.at(0)) == actual_msgs.at(topics.at(0)));
    REQUIRE(msgs.at(topics.at(1)) == actual_msgs.at(topics.at(1)));
    REQUIRE(is_repeated_persistent_flags.size() == actual_is_repeated_persistent_flags.size());
    REQUIRE(is_repeated_persistent_flags.at(topics.at(0)) == actual_is_repeated_persistent_flags.at(topics.at(0)));
    REQUIRE(is_repeated_persistent_flags.at(topics.at(1)) == actual_is_repeated_persistent_flags.at(topics.at(1)));
  }

  SECTION("Filter topics")
  {
    const auto& filter_topic = topics.back();

    OnboardLogReader reader(log_path, {}, {}, decompress_option);
    REQUIRE(reader.open([&filter_topic](const auto& topic) { return topic == filter_topic; }));

    auto actual_msgs = std::map<std::string, std::vector<MsgType>>();
    auto actual_is_repeated_persistent_flags = std::map<std::string, std::vector<bool>>();
    while (auto msg_result = reader.next_message())
    {
      auto msg = msg_result.value();
      if (msg.is_lite_compressed)
      {
        const auto decompress_result = lite_compressor.decompress(msg.data);
        REQUIRE(decompress_result);
        msg.data = decompress_result.value();
        msg.is_lite_compressed = false;
      }
      REQUIRE(
        msg.message_encoding == static_cast<MessageEncoding>(clockwork::LoggingTraits<MsgType>::message_encoding));
      deserialize_tachyon(actual_msgs[std::string(msg.topic)].emplace_back(), msg.data);
      actual_is_repeated_persistent_flags[std::string(msg.topic)].emplace_back(msg.is_repeated_persistent);
    }

    REQUIRE(1 == actual_msgs.size());
    REQUIRE(actual_msgs.contains(filter_topic));
    REQUIRE(msgs.at(filter_topic) == actual_msgs.at(filter_topic));
    REQUIRE(is_repeated_persistent_flags.contains(filter_topic));
    REQUIRE(is_repeated_persistent_flags.at(filter_topic) == actual_is_repeated_persistent_flags.at(filter_topic));
  }

  SECTION("Interval")
  {
    OnboardLogReader reader(log_path, LogInterval(LogTimestamp(4), LogTimestamp(9)), {}, decompress_option);
    REQUIRE(reader.open({}));

    auto actual_timestamps = std::map<std::string, std::vector<LogTimestamp>>();
    auto actual_is_repeated_persistent_flags = std::map<std::string, std::vector<bool>>();
    while (auto msg_result = reader.next_message())
    {
      auto msg = msg_result.value();
      if (msg.is_lite_compressed)
      {
        const auto decompress_result = lite_compressor.decompress(msg.data);
        REQUIRE(decompress_result);
      }
      REQUIRE(
        msg.message_encoding == static_cast<MessageEncoding>(clockwork::LoggingTraits<MsgType>::message_encoding));
      actual_timestamps[std::string(msg.topic)].emplace_back(msg.publish_time);
      actual_is_repeated_persistent_flags[std::string(msg.topic)].emplace_back(msg.is_repeated_persistent);
    }

    auto expected_timestamps = std::map<std::string, std::vector<LogTimestamp>>({
      {topics.at(0), {LogTimestamp(4), LogTimestamp(6), LogTimestamp(8)}},
      {topics.at(1), {LogTimestamp(4), LogTimestamp(5), LogTimestamp(7), LogTimestamp(9)}},
    });
    auto expected_is_repeated_persistent_flags = std::map<std::string, std::vector<bool>>({
      {topics.at(0), {false, false, false}},
      {topics.at(1), {true, false, false, false}},
    });

    REQUIRE(expected_timestamps.size() == actual_timestamps.size());
    REQUIRE(expected_timestamps.at(topics.at(0)) == actual_timestamps.at(topics.at(0)));
    REQUIRE(expected_timestamps.at(topics.at(1)) == actual_timestamps.at(topics.at(1)));
    REQUIRE(expected_is_repeated_persistent_flags.size() == actual_is_repeated_persistent_flags.size());
    REQUIRE(
      expected_is_repeated_persistent_flags.at(topics.at(0)) == actual_is_repeated_persistent_flags.at(topics.at(0)));
    REQUIRE(
      expected_is_repeated_persistent_flags.at(topics.at(1)) == actual_is_repeated_persistent_flags.at(topics.at(1)));
  }

  SECTION("Relative Interval")
  {
    OnboardLogReader reader(
      log_path,
      {},
      RelativeInterval{.start_offset = std::chrono::nanoseconds{4}, .end_offset = std::chrono::nanoseconds{9}},
      decompress_option);
    REQUIRE(reader.open({}));

    auto actual_timestamps = std::map<std::string, std::vector<LogTimestamp>>();
    auto actual_is_repeated_persistent_flags = std::map<std::string, std::vector<bool>>();
    while (auto msg_result = reader.next_message())
    {
      auto msg = msg_result.value();
      if (msg.is_lite_compressed)
      {
        const auto decompress_result = lite_compressor.decompress(msg.data);
        REQUIRE(decompress_result);
      }
      REQUIRE(
        msg.message_encoding == static_cast<MessageEncoding>(clockwork::LoggingTraits<MsgType>::message_encoding));
      actual_timestamps[std::string(msg.topic)].emplace_back(msg.publish_time);
      actual_is_repeated_persistent_flags[std::string(msg.topic)].emplace_back(msg.is_repeated_persistent);
    }

    auto expected_timestamps = std::map<std::string, std::vector<LogTimestamp>>({
      {topics.at(0), {LogTimestamp(4), LogTimestamp(6), LogTimestamp(8)}},
      {topics.at(1), {LogTimestamp(4), LogTimestamp(5), LogTimestamp(7), LogTimestamp(9)}},
    });
    auto expected_is_repeated_persistent_flags = std::map<std::string, std::vector<bool>>({
      {topics.at(0), {false, false, false}},
      {topics.at(1), {true, false, false, false}},
    });

    REQUIRE(expected_timestamps.size() == actual_timestamps.size());
    REQUIRE(expected_timestamps.at(topics.at(0)) == actual_timestamps.at(topics.at(0)));
    REQUIRE(expected_timestamps.at(topics.at(1)) == actual_timestamps.at(topics.at(1)));
    REQUIRE(expected_is_repeated_persistent_flags.size() == actual_is_repeated_persistent_flags.size());
    REQUIRE(
      expected_is_repeated_persistent_flags.at(topics.at(0)) == actual_is_repeated_persistent_flags.at(topics.at(0)));
    REQUIRE(
      expected_is_repeated_persistent_flags.at(topics.at(1)) == actual_is_repeated_persistent_flags.at(topics.at(1)));
  }
}

} // namespace
} // namespace clockwork_logging
