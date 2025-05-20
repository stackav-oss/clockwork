// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/channel_type.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/readers/abstract_log_reader.hh"
#include "clockwork/logging/readers/log_processor.hh"
#include "clockwork/logging/readers/tests/support/test_log_reader.hh"
#include "clockwork/logging/readers/types.hh"
#include "clockwork/logging/tests/support/test_message.hh"
#include "clockwork/logging/writers/logger_status.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/std/expected.hh"

#include <catch2/catch_test_macros.hpp>
#include <fmt10/format.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace clockwork_logging
{
namespace
{

TEST_CASE("LogProcessor topic filter")
{
  auto topics = {"/topic1", "/topic2"};
  auto config = LogReaderConfig{
    .uri = "test_log",
    .interval = {},
    .relative_interval = {},
    .topic_filter = [](auto topic) { return topic.starts_with("/"); },
  };

  REQUIRE(config.topic_filter("/topic1"));
  REQUIRE(config.topic_filter("/topic2"));
  REQUIRE(config.topic_filter("/topic3"));

  const auto filter = LogProcessor::augment_topic_filter(config.topic_filter, topics);

  REQUIRE(filter("/topic1"));
  REQUIRE(filter("/topic2"));
  REQUIRE_FALSE(filter("/topic3"));
}

TEST_CASE("LogProcessor callbacks")
{
  using MsgType = clockwork::Tappy<tests::TestMessage>;

  auto topics = {"/topic1", "/topic2"};
  auto msgs = std::map<std::string, std::vector<TestMsgRecord>>();
  for (int64_t i = 0; i < 10; ++i)
  {
    for (const auto& topic : topics)
    {
      auto& record = msgs[topic].emplace_back();
      record.publish_time = LogTimestamp(i);
      record.msg.get_underlying_message_string().set_truncate(fmt::format("{}{}", topic, i));
    }
  }

  SECTION("one topic")
  {
    const auto* topic = *topics.begin();
    auto actual = std::map<std::string, size_t>();

    REQUIRE(LogProcessor(
              std::make_unique<TestLogReader>(
                "test_log", std::optional<LogInterval>{}, std::optional<RelativeInterval>{}, msgs),
              {})
              .add_tappy_callback<MsgType>(topic, [&](const auto& /*msg*/) { ++actual[topic]; })
              .process());

    auto expected = std::map<std::string, size_t>({{topic, msgs.at(topic).size()}});

    REQUIRE(expected == actual);
  }

  SECTION("one topic callback with timestamp")
  {
    const auto* topic = *topics.begin();
    auto actual = std::map<std::string, std::vector<TestMsgRecord>>();

    REQUIRE(LogProcessor(
              std::make_unique<TestLogReader>(
                "test_log", std::optional<LogInterval>{}, std::optional<RelativeInterval>{}, msgs),
              {})
              .add_tappy_callback<MsgType>(
                topic,
                [&](const auto& publish_time, const auto& msg)
                { actual[topic].push_back(TestMsgRecord{.publish_time = publish_time, .msg = msg}); })
              .process());

    auto expected = std::map<std::string, std::vector<TestMsgRecord>>({{topic, msgs.at(topic)}});

    REQUIRE(expected == actual);
  }

  SECTION("throw on bad message when configured")
  {
    const auto* topic = *topics.begin();
    auto actual = std::map<std::string, std::vector<TestMsgRecord>>();

    // Force a deserialization error by telling the log processor the wrong message type
    CHECK_THROWS(LogProcessor(
                   std::make_unique<TestLogReader>(
                     "test_log", std::optional<LogInterval>{}, std::optional<RelativeInterval>{}, msgs),
                   {})
                   .add_tappy_callback<clockwork::Tappy<LoggerStatus>>(
                     topic, [&]([[maybe_unused]] const auto& publish_time, [[maybe_unused]] const auto& msg) -> void {})
                   .process());

    CHECK_NOTHROW(LogProcessor(
                    std::make_unique<TestLogReader>(
                      "test_log", std::optional<LogInterval>{}, std::optional<RelativeInterval>{}, msgs),
                    {})
                    .add_tappy_callback<clockwork::Tappy<LoggerStatus>>(
                      topic,
                      [&]([[maybe_unused]] const auto& publish_time, [[maybe_unused]] const auto& msg) -> void {},
                      DeserializationErrorBehavior::keep_going)
                    .process());
  }

  SECTION("multiple topics")
  {
    auto actual = std::map<std::string, std::vector<TestMsgRecord>>();

    auto processor = LogProcessor(
      std::make_unique<TestLogReader>(
        "test_log", std::optional<LogInterval>{}, std::optional<RelativeInterval>{}, msgs),
      {});
    for (const auto& topic : topics)
    {
      // NOLINTNEXTLINE(cert-err33-c) False positive
      processor.add_tappy_callback<MsgType>(
        topic,
        [&](const auto& publish_time, const auto& msg) -> void
        { actual[topic].push_back(TestMsgRecord{.publish_time = publish_time, .msg = msg}); });
    }
    REQUIRE(processor.process());

    const auto& expected = msgs;
    REQUIRE(expected == actual);
  }

  SECTION("abort")
  {
    const auto* topic = *topics.begin();
    auto actual = std::map<std::string, std::vector<TestMsgRecord>>();

    LogProcessor processor(
      std::make_unique<TestLogReader>(
        "test_log", std::optional<LogInterval>{}, std::optional<RelativeInterval>{}, msgs),
      {});
    // NOLINTNEXTLINE(cert-err33-c) False positive
    processor.add_tappy_callback<MsgType>(
      topic,
      [&](const auto& publish_time, const auto& msg) -> void
      {
        actual[topic].push_back(TestMsgRecord{.publish_time = publish_time, .msg = msg});
        processor.abort();
      });
    REQUIRE_FALSE(processor.process());

    auto expected = std::map<std::string, std::vector<TestMsgRecord>>({{topic, {*(msgs.at(topic)).begin()}}});

    REQUIRE(expected == actual);
  }

  SECTION("abort then resume")
  {
    const auto* topic = *topics.begin();
    auto actual = std::map<std::string, std::vector<TestMsgRecord>>();

    LogProcessor processor(
      std::make_unique<TestLogReader>(
        "test_log", std::optional<LogInterval>{}, std::optional<RelativeInterval>{}, msgs),
      {});
    // NOLINTNEXTLINE(cert-err33-c) False positive
    processor.add_tappy_callback<MsgType>(
      topic,
      [&](const auto& publish_time, const auto& msg) -> void
      {
        actual[topic].push_back(TestMsgRecord{.publish_time = publish_time, .msg = msg});
        processor.abort();
      });
    REQUIRE_FALSE(processor.process());
    REQUIRE_FALSE(processor.process());

    auto expected = std::map<std::string, std::vector<TestMsgRecord>>(
      {{topic, std::vector<TestMsgRecord>(msgs.at(topic).begin(), msgs.at(topic).begin() + 2)}});

    REQUIRE(expected == actual);
  }

  SECTION("try_get_topic_metadata")
  {
    LogProcessor processor(
      std::make_unique<TestLogReader>(
        "test_log", std::optional<LogInterval>{}, std::optional<RelativeInterval>{}, msgs),
      {});
    auto topic_result = processor.try_get_topic_metadata("/topic1");
    REQUIRE(topic_result);
    REQUIRE(
      topic_result.value() == TopicMetadata{
                                .name = "/topic1",
                                .type = std::string{clockwork::LoggingTraits<MsgType>::schema_name},
                                .message_encoding = clockwork::LoggingTraits<MsgType>::message_encoding,
                                .channel_type = ChannelType::regular,
                                .schema_encoding = clockwork::LoggingTraits<MsgType>::schema_encoding,
                                .schema_definition =
                                  std::string{
                                    clockwork::LoggingTraits<MsgType>::schema_definition.data(),
                                    clockwork::LoggingTraits<MsgType>::schema_definition.size()},
                              });
    topic_result = processor.try_get_topic_metadata("/topic2");
    REQUIRE(topic_result);
    REQUIRE(
      topic_result.value() == TopicMetadata{
                                .name = "/topic2",
                                .type = std::string{clockwork::LoggingTraits<MsgType>::schema_name},
                                .message_encoding = clockwork::LoggingTraits<MsgType>::message_encoding,
                                .channel_type = ChannelType::regular,
                                .schema_encoding = clockwork::LoggingTraits<MsgType>::schema_encoding,
                                .schema_definition =
                                  std::string{
                                    clockwork::LoggingTraits<MsgType>::schema_definition.data(),
                                    clockwork::LoggingTraits<MsgType>::schema_definition.size()},
                              });
    REQUIRE_FALSE(processor.try_get_topic_metadata("INVALID_TOPIC"));
  }
}

TEST_CASE("LogProcessor next")
{
  // Prepare the messages we expect to read back
  auto topics = {"/topic1", "/topic2"};
  auto msgs = std::map<std::string, std::vector<TestMsgRecord>>();
  for (int64_t i = 0; i < 10; ++i)
  {
    for (const auto& topic : topics)
    {
      auto& record = msgs[topic].emplace_back();
      record.publish_time = LogTimestamp(i);
      record.msg.get_underlying_message_string().set_truncate(fmt::format("{}{}", topic, i));
    }
  }

  auto processor = LogProcessor(
    std::make_unique<TestLogReader>("test_log", std::optional<LogInterval>{}, std::optional<RelativeInterval>{}, msgs),
    {});

  // Require that we are reading the messages as expected
  for (int64_t i = 0; i < 10; ++i)
  {
    for (const auto& topic : topics)
    {
      auto msg = processor.next();
      REQUIRE(msg);
      REQUIRE(msg->publish_time.get_nanoseconds() == i);
      REQUIRE(msg->topic == topic);
    }
  }

  // Since there are no messages left to read, we get empty responses from this point onwards
  REQUIRE(!processor.next()); // empty
  REQUIRE(!processor.next()); // still empty
}

} // namespace
} // namespace clockwork_logging
