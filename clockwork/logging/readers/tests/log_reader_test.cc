// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/channel_type.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/readers/abstract_log_reader.hh"
#include "clockwork/logging/readers/log_reader.hh"
#include "clockwork/logging/readers/serialization.hh"
#include "clockwork/logging/readers/tests/support/test_log_reader.hh"
#include "clockwork/logging/readers/types.hh"
#include "clockwork/logging/tests/support/test_message.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/std/expected.hh"

#include <catch2/catch_test_macros.hpp>
#include <fmt10/format.h>

#include <array>
#include <cstdint>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

namespace clockwork_logging
{
namespace
{

TEST_CASE("LogReader traits")
{
  // std::input_or_output_iterator requires concepts so instead we just check that it's tagged with input_iterator_tag
  static_assert(std::is_same_v<LogReader::Iterator::iterator_category, std::input_iterator_tag>);
  static_assert(std::ranges::input_range<LogReader>);
}

TEST_CASE("LogReader iterator")
{
  using MsgType = clockwork::Tappy<tests::TestMessage>;

  const auto* topic = "topic";
  auto msgs = std::map<std::string, std::vector<TestMsgRecord>>();
  for (int64_t i = 0; i < 10; ++i)
  {
    auto& record = msgs[topic].emplace_back();
    record.publish_time = LogTimestamp(i);
    record.msg.get_underlying_message_string().set_truncate(fmt::format("{}", topic, i));
  }

  SECTION("topics")
  {
    auto reader = LogReader(
      std::make_unique<TestLogReader>(
        "test_log", std::optional<LogInterval>{}, std::optional<RelativeInterval>{}, msgs));
    auto expected = std::vector<TopicMetadata>({
      {
        .name = "topic",
        .type = std::string{clockwork::LoggingTraits<MsgType>::schema_name},
        .message_encoding = clockwork::LoggingTraits<MsgType>::message_encoding,
        .channel_type = ChannelType::regular,
        .schema_encoding = clockwork::LoggingTraits<MsgType>::schema_encoding,
        .schema_definition =
          std::string{
            clockwork::LoggingTraits<MsgType>::schema_definition.data(),
            clockwork::LoggingTraits<MsgType>::schema_definition.size()},
      },
    });
    auto actual = reader.get_metadata();
    REQUIRE(expected == actual);
  }

  SECTION("get_metadata")
  {
    auto reader = LogReader(
      std::make_unique<TestLogReader>(
        "test_log", std::optional<LogInterval>{}, std::optional<RelativeInterval>{}, msgs));
    REQUIRE(reader.get_metrics() == jewels::unexpected(LogError::not_implemented));
  }

  SECTION("all messages")
  {
    auto reader = LogReader(
      std::make_unique<TestLogReader>(
        "test_log", std::optional<LogInterval>{}, std::optional<RelativeInterval>{}, msgs));
    REQUIRE(reader.open({}));

    auto actual = std::map<std::string, std::vector<TestMsgRecord>>();
    for (const auto& logged_msg : reader)
    {
      auto record = TestMsgRecord{.publish_time = logged_msg.publish_time, .msg = MsgType()};
      deserialize_tachyon<MsgType>(record.msg, logged_msg.data);

      actual[std::string(logged_msg.topic)].push_back(record);
    }

    REQUIRE(msgs == actual);
  }
}

} // namespace
} // namespace clockwork_logging
