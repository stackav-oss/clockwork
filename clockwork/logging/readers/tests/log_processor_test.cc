// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/channel_type_clk_cc.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/message_encoding_clk_cc.hh"
#include "clockwork/logging/offboard/writer.hh"
#include "clockwork/logging/readers/abstract_log_reader.hh"
#include "clockwork/logging/readers/log_processor.hh"
#include "clockwork/logging/readers/tests/support/test_log_reader.hh"
#include "clockwork/logging/readers/types.hh"
#include "clockwork/logging/schema_encoding_clk_cc.hh"
#include "clockwork/logging/tests/support/test_message_clk_cc.hh"
#include "clockwork/logging/writers/logger_status_clk_cc.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/serialization/cpp/tachyon_upgrader.hh"
#include "clockwork/serialization/py/tests/support/simple_schema_v1_clk_cc.hh"
#include "clockwork/serialization/py/tests/support/simple_schema_v2_clk_cc.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/tmp_directory_guard.hh"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <memory_resource>
#include <optional>
#include <string>
#include <string_view>
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

TEST_CASE("Interface")
{
  constexpr std::string_view log_uri{"test_log"};
  const std::map<std::string, std::vector<TestMsgRecord>> msgs{};
  const LogProcessor processor(
    std::make_unique<TestLogReader>(log_uri, std::optional<LogInterval>{}, std::optional<RelativeInterval>{}, msgs),
    {});
  REQUIRE(processor.log_uri() == log_uri);
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
              .add_tappy_callback<MsgType>(topic, [&actual, &topic](const auto& /*msg*/) { ++actual[topic]; })
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
                [&actual, &topic](const auto& publish_time, const auto& msg)
                { actual[topic].push_back(TestMsgRecord{.publish_time = publish_time, .msg = msg}); })
              .process());

    auto expected = std::map<std::string, std::vector<TestMsgRecord>>({{topic, msgs.at(topic)}});

    REQUIRE(expected == actual);
  }

  SECTION("one topic callback with logged message metadata")
  {
    const auto* topic = *topics.begin();
    auto actual = std::map<std::string, std::vector<TestMsgRecord>>();

    REQUIRE(LogProcessor(
              std::make_unique<TestLogReader>(
                "test_log", std::optional<LogInterval>{}, std::optional<RelativeInterval>{}, msgs),
              {})
              .add_tappy_msg_callback<MsgType>(
                topic,
                [&actual, &topic](const LoggedMessage& logged_message, const MsgType& msg)
                {
                  CHECK(logged_message.topic == topic);
                  CHECK(
                    logged_message.sequence_number ==
                    static_cast<uint32_t>(logged_message.publish_time.get_nanoseconds()));
                  CHECK(logged_message.log_time == logged_message.publish_time);
                  actual[topic].push_back(TestMsgRecord{.publish_time = logged_message.publish_time, .msg = msg});
                })
              .process());

    const auto expected = std::map<std::string, std::vector<TestMsgRecord>>({{topic, msgs.at(topic)}});
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
        [&actual, &topic](const auto& publish_time, const auto& msg) -> void
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
      [&actual, &topic, &processor](const auto& publish_time, const auto& msg) -> void
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
      [&actual, &topic, &processor](const auto& publish_time, const auto& msg) -> void
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
      topic_result.value() ==
      TopicMetadata{
        .name = "/topic1",
        .type = std::string{clockwork::LoggingTraits<MsgType>::schema_name},
        .message_encoding = static_cast<MessageEncoding>(clockwork::LoggingTraits<MsgType>::message_encoding),
        .channel_type = ChannelType::regular,
        .schema_encoding = static_cast<SchemaEncoding>(clockwork::LoggingTraits<MsgType>::schema_encoding),
        .schema_definition =
          std::string{
            clockwork::LoggingTraits<MsgType>::schema_definition.data(),
            clockwork::LoggingTraits<MsgType>::schema_definition.size()},
        .is_amended = false,
      });
    topic_result = processor.try_get_topic_metadata("/topic2");
    REQUIRE(topic_result);
    REQUIRE(
      topic_result.value() ==
      TopicMetadata{
        .name = "/topic2",
        .type = std::string{clockwork::LoggingTraits<MsgType>::schema_name},
        .message_encoding = static_cast<MessageEncoding>(clockwork::LoggingTraits<MsgType>::message_encoding),
        .channel_type = ChannelType::regular,
        .schema_encoding = static_cast<SchemaEncoding>(clockwork::LoggingTraits<MsgType>::schema_encoding),
        .schema_definition =
          std::string{
            clockwork::LoggingTraits<MsgType>::schema_definition.data(),
            clockwork::LoggingTraits<MsgType>::schema_definition.size()},
        .is_amended = false,
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

TEST_CASE("LogProcesser - upgrade schema")
{
  constexpr auto test_log_name = "test_log";

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto test_log_path = test_dir.get_path() / test_log_name;
  offboard::Writer writer{memory_resource};

  constexpr LogTimestamp time1{std::chrono::seconds(1)};
  constexpr LogTimestamp time2{std::chrono::seconds(2)};

  constexpr auto channel_name1 = "channel1";
  constexpr auto channel_name2 = "channel2";

  clockwork::Tappy<clockwork::tests::SimpleSchemaV1> message1{};
  message1.set_integer_field(42);

  clockwork::Tappy<clockwork::tests::SimpleSchemaV2> message2{};
  message2.set_integer_field(42);
  message2.get_underlying_string_field().set_truncate("test");

  REQUIRE(writer.open(test_log_path.string()));
  REQUIRE(writer.create_channel<clockwork::Tappy<clockwork::tests::SimpleSchemaV1>>(channel_name1));
  REQUIRE(writer.create_channel<clockwork::Tappy<clockwork::tests::SimpleSchemaV2>>(channel_name2));

  REQUIRE(writer.write(channel_name1, 1U, time1, time1, message1));
  REQUIRE(writer.write(channel_name2, 2U, time2, time2, message2));

  const auto close_result = writer.close();
  REQUIRE(close_result);

  SECTION("callback")
  {
    auto channel1_count = 0U;
    auto channel2_count = 0U;

    auto config = LogReaderConfig{
      .uri = std::string{test_log_path.c_str()},
      .interval = {},
      .relative_interval = {},
      .topic_filter = {},
    };

    REQUIRE(LogProcessor(config)
              .add_tappy_callback<clockwork::Tappy<clockwork::tests::SimpleSchemaV2>>(
                channel_name1,
                [&channel1_count](const auto& msg)
                {
                  channel1_count += 1U;
                  CHECK(msg.get_integer_field() == 42);
                  CHECK(msg.get_string_field().empty());
                })
              .add_tappy_callback<clockwork::Tappy<clockwork::tests::SimpleSchemaV2>>(
                channel_name2,
                [&channel2_count](const auto& msg)
                {
                  channel2_count += 1U;
                  CHECK(msg.get_integer_field() == 42);
                  CHECK(msg.get_string_field() == "test");
                })
              .process());

    REQUIRE(channel1_count == 1U);
    REQUIRE(channel2_count == 1U);
  }

  SECTION("callback with timestamp")
  {
    auto channel1_count = 0U;
    auto channel2_count = 0U;

    auto config = LogReaderConfig{
      .uri = std::string{test_log_path.c_str()},
      .interval = {},
      .relative_interval = {},
      .topic_filter = {},
    };

    REQUIRE(LogProcessor(config)
              .add_tappy_callback<clockwork::Tappy<clockwork::tests::SimpleSchemaV2>>(
                channel_name1,
                [&channel1_count](const LogTimestamp& timestamp, const auto& msg)
                {
                  channel1_count += 1U;
                  CHECK(timestamp.get_duration() == std::chrono::seconds(1));
                  CHECK(msg.get_integer_field() == 42);
                  CHECK(msg.get_string_field().empty());
                })
              .add_tappy_callback<clockwork::Tappy<clockwork::tests::SimpleSchemaV2>>(
                channel_name2,
                [&channel2_count](const LogTimestamp& timestamp, const auto& msg)
                {
                  channel2_count += 1U;
                  CHECK(timestamp.get_duration() == std::chrono::seconds(2));
                  CHECK(msg.get_integer_field() == 42);
                  CHECK(msg.get_string_field() == "test");
                })
              .process());

    REQUIRE(channel1_count == 1U);
    REQUIRE(channel2_count == 1U);
  }
}

} // namespace
} // namespace clockwork_logging
