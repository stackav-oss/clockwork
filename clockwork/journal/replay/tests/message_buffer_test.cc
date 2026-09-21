// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/journal/replay/message_buffer.hh"
#include "clockwork/journal/replay/replay_metadata.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/readers/types.hh"
#include "clockwork/logging/tests/support/test_message_clk_cc.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/container/tap/var_string.hh"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace clockwork::journal::replay
{
namespace
{

using MessageType = clockwork::Tappy<clockwork_logging::tests::TestMessage>;

TEST_CASE("MessageBuffer retains required sequence numbers")
{
  constexpr auto key = MessageKey{2U};
  const auto requirements = std::vector{ChannelRequirements{
    .channel_name = "input",
    .messages = {{.key = key, .reference_count = 1U}},
  }};
  auto buffer_result = jewels::FactoryResult<MessageBuffer<MessageType>>{};
  REQUIRE(jewels::ok(MessageBuffer<MessageType>::try_make(jewels::FactoryOut{buffer_result}, requirements)));
  auto& buffer = *buffer_result;
  auto logged_message = clockwork_logging::LoggedMessage{};
  auto logged_channel_name = std::string{"input"};
  logged_message.topic = logged_channel_name;
  logged_message.sequence_number = 2U;
  logged_message.publish_time = clockwork_logging::LogTimestamp{19};
  logged_message.log_time = clockwork_logging::LogTimestamp{20};
  auto typed_message = MessageType{};
  typed_message.get_underlying_message_string().set_truncate("payload");
  CHECK(buffer.push(logged_message, typed_message));
  CHECK_FALSE(buffer.push(logged_message, typed_message));
  CHECK(buffer.size() == 1U);
  logged_channel_name = "modified";
  typed_message.get_underlying_message_string().set_truncate("modified");

  std::shared_ptr<const ReplayMessage<MessageType>> replay_message;
  REQUIRE(jewels::ok(buffer.get(jewels::Out{replay_message}, "input", key)));
  CHECK(replay_message->message.get_message_string() == "payload");
  CHECK(replay_message->channel_name == "input");
  CHECK(replay_message->sequence_number == 2U);
  CHECK(replay_message->publish_time == clockwork_logging::LogTimestamp{19});
  REQUIRE(jewels::ok(buffer.consume("input", key)));
  CHECK(buffer.size() == 0U);
  CHECK(replay_message->sequence_number == key);
}

TEST_CASE("MessageBuffer drops messages without requirements")
{
  constexpr auto required_key = MessageKey{2U};
  const auto requirements = std::vector{ChannelRequirements{
    .channel_name = "input",
    .messages = {{.key = required_key, .reference_count = 1U}},
  }};
  auto buffer_result = jewels::FactoryResult<MessageBuffer<MessageType>>{};
  REQUIRE(jewels::ok(MessageBuffer<MessageType>::try_make(jewels::FactoryOut{buffer_result}, requirements)));
  auto& buffer = *buffer_result;
  auto logged_message = clockwork_logging::LoggedMessage{};
  logged_message.topic = "input";
  logged_message.sequence_number = 3U;

  CHECK_FALSE(buffer.push(logged_message, MessageType{}));
  CHECK(buffer.size() == 0U);
  CHECK_FALSE(buffer.contains("input", 3U));
  std::shared_ptr<const ReplayMessage<MessageType>> replay_message;
  CHECK(jewels::fails(buffer.get(jewels::Out{replay_message}, "input", 3U)));

  logged_message.topic = "other";
  logged_message.sequence_number = static_cast<uint32_t>(required_key);
  CHECK_FALSE(buffer.push(logged_message, MessageType{}));
  CHECK(buffer.size() == 0U);
  CHECK_FALSE(buffer.contains("other", required_key));
  CHECK(jewels::fails(buffer.get(jewels::Out{replay_message}, "other", required_key)));
}

TEST_CASE("MessageBuffer distinguishes channel and sequence keys")
{
  constexpr auto first_key = MessageKey{1U};
  constexpr auto second_key = MessageKey{2U};
  const auto requirements = std::vector{
    ChannelRequirements{
      .channel_name = "input",
      .messages =
        {
          {.key = first_key, .reference_count = 1U},
          {.key = second_key, .reference_count = 1U},
        },
    },
    ChannelRequirements{
      .channel_name = "other",
      .messages = {{.key = first_key, .reference_count = 1U}},
    },
  };
  auto buffer_result = jewels::FactoryResult<MessageBuffer<MessageType>>{};
  REQUIRE(jewels::ok(MessageBuffer<MessageType>::try_make(jewels::FactoryOut{buffer_result}, requirements)));
  auto& buffer = *buffer_result;
  std::shared_ptr<const ReplayMessage<MessageType>> replay_message;
  CHECK_FALSE(buffer.contains("input", first_key));
  CHECK(jewels::fails(buffer.get(jewels::Out{replay_message}, "input", first_key)));

  auto logged_message = clockwork_logging::LoggedMessage{};
  logged_message.topic = "input";
  logged_message.sequence_number = static_cast<uint32_t>(first_key);
  REQUIRE(buffer.push(logged_message, MessageType{}));

  CHECK(buffer.contains("input", first_key));
  CHECK_FALSE(buffer.contains("input", second_key));
  CHECK_FALSE(buffer.contains("other", first_key));
  CHECK(jewels::ok(buffer.get(jewels::Out{replay_message}, "input", first_key)));
  CHECK(jewels::fails(buffer.get(jewels::Out{replay_message}, "input", second_key)));
  CHECK(jewels::fails(buffer.get(jewels::Out{replay_message}, "other", first_key)));
}

TEST_CASE("MessageBuffer consumes requirements for messages that were not buffered")
{
  constexpr auto key = MessageKey{1U};
  const auto requirements = std::vector{ChannelRequirements{
    .channel_name = "input",
    .messages = {{.key = key, .reference_count = 1U}},
  }};
  auto buffer_result = jewels::FactoryResult<MessageBuffer<MessageType>>{};
  REQUIRE(jewels::ok(MessageBuffer<MessageType>::try_make(jewels::FactoryOut{buffer_result}, requirements)));
  auto& buffer = *buffer_result;

  REQUIRE(jewels::ok(buffer.consume("input", key)));
  CHECK(buffer.size() == 0U);
  CHECK_FALSE(buffer.contains("input", key));

  auto logged_message = clockwork_logging::LoggedMessage{};
  logged_message.topic = "input";
  logged_message.sequence_number = static_cast<uint32_t>(key);
  CHECK_FALSE(buffer.push(logged_message, MessageType{}));
  CHECK(jewels::fails(buffer.consume("input", key)));
}

TEST_CASE("MessageBuffer releases a persistent message after its last execution")
{
  constexpr auto key = MessageKey{4U};
  const auto requirements = std::vector{ChannelRequirements{
    .channel_name = "persistent",
    .messages = {{.key = key, .reference_count = 2U}},
  }};
  auto buffer_result = jewels::FactoryResult<MessageBuffer<MessageType>>{};
  REQUIRE(jewels::ok(MessageBuffer<MessageType>::try_make(jewels::FactoryOut{buffer_result}, requirements)));
  auto& buffer = *buffer_result;
  auto logged_message = clockwork_logging::LoggedMessage{};
  logged_message.topic = "persistent";
  logged_message.sequence_number = 4U;
  REQUIRE(buffer.push(logged_message, MessageType{}));

  REQUIRE(jewels::ok(buffer.consume("persistent", key)));
  CHECK(buffer.contains("persistent", key));
  REQUIRE(jewels::ok(buffer.consume("persistent", key)));
  CHECK(!buffer.contains("persistent", key));
  CHECK(jewels::fails(buffer.consume("persistent", key)));
}

TEST_CASE("MessageBuffer rejects duplicate requirements")
{
  constexpr auto key = MessageKey{1U};
  const auto requirements = std::vector{ChannelRequirements{
    .channel_name = "input",
    .messages =
      {
        {.key = key, .reference_count = 1U},
        {.key = key, .reference_count = 1U},
      },
  }};
  auto buffer_result = jewels::FactoryResult<MessageBuffer<MessageType>>{};

  CHECK(jewels::fails(MessageBuffer<MessageType>::try_make(jewels::FactoryOut{buffer_result}, requirements)));
  CHECK_FALSE(buffer_result.has_value());
}

TEST_CASE("MessageBuffer rejects requirements without execution references")
{
  const auto requirements = std::vector{ChannelRequirements{
    .channel_name = "input",
    .messages = {{.key = 1U, .reference_count = 0U}},
  }};
  auto buffer_result = jewels::FactoryResult<MessageBuffer<MessageType>>{};

  CHECK(jewels::fails(MessageBuffer<MessageType>::try_make(jewels::FactoryOut{buffer_result}, requirements)));
  CHECK_FALSE(buffer_result.has_value());
}

} // namespace
} // namespace clockwork::journal::replay
