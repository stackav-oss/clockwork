// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/channel_type_clk_cc.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/message_encoding_clk_cc.hh"
#include "clockwork/logging/readers/mcap_log_reader.hh"
#include "clockwork/logging/readers/types.hh"
#include "clockwork/logging/schema_encoding_clk_cc.hh"
#include "clockwork/logging/tests/support/test_message_clk_cc.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/std/expected.hh"
#include "jewels/std/span.hh"
#include "jewels/testing/tmp_directory_guard.hh"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>
#include <mcap/errors.hpp>
#include <mcap/types.hpp>
#include <mcap/writer.hpp>
#include <wise_enum.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
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

TEST_CASE("Mcap Log Reader")
{
  using MsgType = clockwork::Tappy<tests::TestMessage>;

  const jewels::testing::TmpDirectoryGuard test_dir;

  // Write a simple test log.

  const auto log_uri = (test_dir.get_path() / "test_log.mcap").string();
  mcap::McapWriter writer;
  REQUIRE(writer.open(log_uri, mcap::McapWriterOptions("")).ok());

  mcap::Schema schema{
    clockwork::LoggingTraits<MsgType>::schema_name,
    wise_enum::to_string(clockwork::LoggingTraits<MsgType>::schema_encoding),
    std::string_view{
      clockwork::LoggingTraits<MsgType>::schema_definition.data(),
      clockwork::LoggingTraits<MsgType>::schema_definition.size()}};
  writer.addSchema(schema);
  mcap::Channel channel1{
    "channel1", wise_enum::to_string(clockwork::LoggingTraits<MsgType>::message_encoding), schema.id};
  writer.addChannel(channel1);
  mcap::Channel channel2{
    "channel2", wise_enum::to_string(clockwork::LoggingTraits<MsgType>::message_encoding), schema.id};
  writer.addChannel(channel2);

  auto msgs = std::map<std::string, std::vector<MsgType>>();
  for (uint32_t i = 0; i < 10U; ++i)
  {
    for (const auto& channel : {channel1, channel2})
    {
      MsgType test_message{};
      test_message.get_underlying_message_string().set_truncate(fmt::format("Test{}", i));
      mcap::Message message{};
      message.channelId = channel.id;
      message.sequence = i;
      message.logTime = i;
      message.publishTime = i;
      message.dataSize = sizeof(test_message);
      message.data = std::as_bytes(jewels::as_single_item_span(test_message)).data();
      REQUIRE(writer.write(message).ok());

      msgs[channel.topic].push_back(test_message);
    }
  }

  writer.close();

  SECTION("Topics")
  {
    McapLogReader reader(log_uri, {}, {});

    auto expected = std::vector<TopicMetadata>(
      {{
         .name = "channel1",
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
         .name = "channel2",
         .type = std::string{clockwork::LoggingTraits<MsgType>::schema_name},
         .message_encoding = static_cast<MessageEncoding>(clockwork::LoggingTraits<MsgType>::message_encoding),
         .channel_type = ChannelType::regular,
         .schema_encoding = static_cast<SchemaEncoding>(clockwork::LoggingTraits<MsgType>::schema_encoding),
         .schema_definition =
           std::string{
             clockwork::LoggingTraits<MsgType>::schema_definition.data(),
             clockwork::LoggingTraits<MsgType>::schema_definition.size()},
       }});

    const auto actual = reader.get_metadata();
    REQUIRE(expected == actual);

    const auto channel1_metadata = reader.get_channel_metadata("channel1");
    REQUIRE(channel1_metadata);
    REQUIRE(channel1_metadata == expected.front());

    const auto channel2_metadata = reader.get_channel_metadata("channel2");
    REQUIRE(channel2_metadata);
    REQUIRE(channel2_metadata == expected.back());

    REQUIRE(reader.get_channels() == std::vector<std::string>{"channel1", "channel2"});
  }

  SECTION("Metrics")
  {
    McapLogReader reader(log_uri, {}, {});

    const auto metrics_result = reader.get_metrics();
    REQUIRE(metrics_result);
    const auto& metrics = metrics_result.value();
    REQUIRE(metrics.transmit_time_interval == LogInterval{LogTimestamp{0}, LogTimestamp{9}});
    REQUIRE(metrics.message_count == 20U);
    REQUIRE(metrics.byte_count == 0U);
    REQUIRE(metrics.topic_metrics.size() == 2U);
    REQUIRE(metrics.topic_metrics.at(0U).topic == "channel1");
    REQUIRE(metrics.topic_metrics.at(0U).transmit_time_interval == LogInterval{});
    REQUIRE(metrics.topic_metrics.at(0U).message_count == 10U);
    REQUIRE(metrics.topic_metrics.at(0U).byte_count == 0U);
    REQUIRE(metrics.topic_metrics.at(1U).topic == "channel2");
    REQUIRE(metrics.topic_metrics.at(1U).transmit_time_interval == LogInterval{});
    REQUIRE(metrics.topic_metrics.at(1U).message_count == 10U);
    REQUIRE(metrics.topic_metrics.at(1U).byte_count == 0U);
  }

  SECTION("Read all messages")
  {
    McapLogReader reader(log_uri, {}, {});
    REQUIRE(reader.open({}));

    auto actual = std::map<std::string, std::vector<MsgType>>();
    while (auto msg = reader.next_message())
    {
      REQUIRE(msg);
      REQUIRE(msg->message_encoding == MessageEncoding::tachyon);
      std::memcpy(&actual[std::string(msg->topic)].emplace_back(), msg->data.data(), msg->data.size());
    }

    REQUIRE(msgs == actual);
  }

  SECTION("Filter topics")
  {
    McapLogReader reader(log_uri, {}, {});
    REQUIRE(reader.open([](const auto& topic) { return topic == "channel1"; }));

    auto actual = std::map<std::string, std::vector<MsgType>>();
    while (auto msg = reader.next_message())
    {
      REQUIRE(msg);
      REQUIRE(msg->message_encoding == MessageEncoding::tachyon);
      std::memcpy(&actual[std::string(msg->topic)].emplace_back(), msg->data.data(), msg->data.size());
    }

    REQUIRE(1 == actual.size());
    REQUIRE(actual.contains("channel1"));
    REQUIRE(msgs.at("channel1") == actual.at("channel1"));
  }

  SECTION("Interval")
  {
    McapLogReader reader(log_uri, LogInterval(LogTimestamp(2), LogTimestamp(4)), {});
    REQUIRE(reader.open({}));

    auto actual = std::map<std::string, std::vector<LogTimestamp>>();
    while (auto msg = reader.next_message())
    {
      REQUIRE(msg);
      REQUIRE(msg->message_encoding == MessageEncoding::tachyon);
      actual[std::string(msg->topic)].emplace_back(msg->publish_time);
    }

    auto expected = std::map<std::string, std::vector<LogTimestamp>>({
      {"channel1", {LogTimestamp(2), LogTimestamp(3)}},
      {"channel2", {LogTimestamp(2), LogTimestamp(3)}},
    });

    REQUIRE(expected == actual);
  }

  SECTION("Relative Interval")
  {
    McapLogReader reader(
      log_uri,
      {},
      RelativeInterval{.start_offset = std::chrono::nanoseconds{2}, .end_offset = std::chrono::nanoseconds{4}});
    REQUIRE(reader.open({}));

    auto actual = std::map<std::string, std::vector<LogTimestamp>>();
    while (auto msg = reader.next_message())
    {
      REQUIRE(msg);
      REQUIRE(msg->message_encoding == MessageEncoding::tachyon);
      actual[std::string(msg->topic)].emplace_back(msg->publish_time);
    }

    auto expected = std::map<std::string, std::vector<LogTimestamp>>({
      {"channel1", {LogTimestamp(2), LogTimestamp(3)}},
      {"channel2", {LogTimestamp(2), LogTimestamp(3)}},
    });

    REQUIRE(expected == actual);
  }
}

} // namespace
} // namespace clockwork_logging
