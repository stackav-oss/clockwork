// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/readers/tests/support/test_log_reader.hh"

#include "clockwork/logging/channel_type.hh"
#include "jewels/std/expected.hh"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <ranges>
#include <span>
#include <utility>

namespace clockwork_logging
{

TestLogReader::TestLogReader(
  std::string_view log_uri,
  std::optional<LogInterval> maybe_log_interval,
  std::optional<RelativeInterval> maybe_relative_interval,
  std::map<std::string, std::vector<TestMsgRecord>> msg_records)
  : AbstractLogReader(log_uri, maybe_log_interval, maybe_relative_interval), msg_records_(std::move(msg_records))
{
  for (const auto& [topic, records] : msg_records_)
  {
    for (const auto& record : records)
    {
      auto logged_msg = LoggedMessage{
        .topic = topic,
        .sequence_number = static_cast<uint32_t>(record.publish_time.get_nanoseconds()),
        .publish_time = record.publish_time,
        .log_time = record.publish_time,
        .header = {},
        .data = std::as_bytes(std::span{&record.msg, 1U}),
      };

      logged_msgs_.push_back(logged_msg);

      if (record.publish_time < log_start_time_)
      {
        log_start_time_ = LogTimestamp{record.publish_time};
      }
      if (record.publish_time > log_end_time_)
      {
        log_end_time_ = LogTimestamp{record.publish_time};
      }
    }
  }

  std::ranges::stable_sort(
    logged_msgs_, [](const auto& lhs, const auto& rhs) { return lhs.publish_time < rhs.publish_time; });
}

TestLogReader::~TestLogReader()
{
  REQUIRE((!opened_ || closed_));
}

std::string TestLogReader::type() const
{
  return "test_reader";
}

LogExpected<void> TestLogReader::open(const std::function<bool(std::string_view)>& /*topic_filter*/)
{
  REQUIRE(!opened_);
  REQUIRE(!closed_);
  opened_ = true;
  return {};
}

LogExpected<void> TestLogReader::close()
{
  REQUIRE(opened_);
  REQUIRE(!closed_);
  closed_ = true;
  return {};
}

std::vector<std::string> TestLogReader::get_channels()
{
  auto channels = std::vector<std::string>();
  for (const auto& name : std::views::keys(msg_records_))
  {
    channels.emplace_back(name);
  }
  return channels;
}

std::vector<TopicMetadata> TestLogReader::get_metadata()
{
  auto topics = std::vector<TopicMetadata>();
  for (const auto& [name, record] : msg_records_)
  {
    topics.emplace_back(TopicMetadata{
      .name = name,
      .type = std::string{clockwork::LoggingTraits<clockwork::Tappy<tests::TestMessage>>::schema_name},
      .message_encoding = clockwork::LoggingTraits<clockwork::Tappy<tests::TestMessage>>::message_encoding,
      .channel_type = ChannelType::regular,
      .schema_encoding = clockwork::LoggingTraits<clockwork::Tappy<tests::TestMessage>>::schema_encoding,
      .schema_definition =
        std::string{
          clockwork::LoggingTraits<clockwork::Tappy<tests::TestMessage>>::schema_definition.data(),
          clockwork::LoggingTraits<clockwork::Tappy<tests::TestMessage>>::schema_definition.size()},
    });
  }
  return topics;
}

LogExpected<TopicMetadata> TestLogReader::get_channel_metadata(std::string_view channel_name)
{
  for (const auto& [name, record] : msg_records_)
  {
    if (name == channel_name)
    {
      return TopicMetadata{
        .name = name,
        .type = std::string{clockwork::LoggingTraits<clockwork::Tappy<tests::TestMessage>>::schema_name},
        .message_encoding = clockwork::LoggingTraits<clockwork::Tappy<tests::TestMessage>>::message_encoding,
        .channel_type = ChannelType::regular,
        .schema_encoding = clockwork::LoggingTraits<clockwork::Tappy<tests::TestMessage>>::schema_encoding,
        .schema_definition =
          std::string{
            clockwork::LoggingTraits<clockwork::Tappy<tests::TestMessage>>::schema_definition.data(),
            clockwork::LoggingTraits<clockwork::Tappy<tests::TestMessage>>::schema_definition.size()},
      };
    }
  }
  return jewels::unexpected(LogError::unknown_channel);
}

LogExpected<LogTimestamp> TestLogReader::start_time()
{
  return log_start_time_;
}

LogExpected<LogTimestamp> TestLogReader::end_time()
{
  return log_start_time_;
}

std::optional<LoggedMessage> TestLogReader::next_message()
{
  REQUIRE(opened_);
  REQUIRE(!closed_);

  if (!logged_msg_iter_)
  {
    logged_msg_iter_ = logged_msgs_.begin();
    if (*logged_msg_iter_ == logged_msgs_.end())
    {
      return {};
    }
    return **logged_msg_iter_;
  }

  ++(*logged_msg_iter_);
  if (*logged_msg_iter_ == logged_msgs_.end())
  {
    return {};
  }
  return **logged_msg_iter_;
}

} // namespace clockwork_logging
