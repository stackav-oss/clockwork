// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/message_encoding.hh"
#include "clockwork/logging/offboard/types.hh"
#include "clockwork/logging/onboard/types.hh"
#include "clockwork/logging/readers/serialization.hh"
#include "clockwork/logging/readers/types.hh"
#include "clockwork/logging/tests/support/test_message.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/container/tap/var_string.hh"

#include <catch2/catch_test_macros.hpp>
#include <fmt10/format.h>

#include <span>

namespace clockwork_logging
{
namespace
{

TEST_CASE("Clockwork Serialization")
{
  using MsgType = clockwork::Tappy<tests::TestMessage>;

  auto original = MsgType();
  original.get_underlying_message_string().set_truncate("test");

  auto logged_message = LoggedMessage{
    .topic = "/test",
    .sequence_number = 10U,
    .publish_time = LogTimestamp(10),
    .log_time = LogTimestamp(10),
    .header = {},
    .data = std::as_bytes(std::span(&original, 1U)),
    .message_encoding = MessageEncoding::tachyon,
  };

  auto deserialized = MsgType();
  deserialize_tachyon(deserialized, logged_message);

  REQUIRE(original == deserialized);
}

TEST_CASE("Clockwork Serialization, onboard logged message")
{
  using MsgType = clockwork::Tappy<tests::TestMessage>;

  auto original = MsgType();
  original.get_underlying_message_string().set_truncate("test");

  auto logged_message = onboard::LoggedMessage{
    .channel_name = "/test",
    .sequence_number = 10U,
    .log_time = LogTimestamp(10),
    .message_time = LogTimestamp(10),
    .header = {},
    .data = std::as_bytes(std::span(&original, 1U)),
    .message_type = onboard::LoggedMessageType::regular,
    .message_encoding = MessageEncoding::tachyon,
    .is_lite_compressed = false,
  };

  auto deserialized = MsgType();
  deserialize_tachyon(deserialized, logged_message);

  REQUIRE(original == deserialized);
}

TEST_CASE("Clockwork Serialization, offboard logged message")
{
  using MsgType = clockwork::Tappy<tests::TestMessage>;

  auto original = MsgType();
  original.get_underlying_message_string().set_truncate("test");

  auto logged_message = offboard::LoggedMessage{
    .channel_name = "/test",
    .sequence_number = 10U,
    .log_time = LogTimestamp(10),
    .transmit_time = LogTimestamp(10),
    .header = {},
    .data = std::as_bytes(std::span(&original, 1U)),
    .is_repeated_persistent = false,
    .message_encoding = MessageEncoding::tachyon,
  };

  auto deserialized = MsgType();
  deserialize_tachyon(deserialized, logged_message);

  REQUIRE(original == deserialized);
}

} // namespace
} // namespace clockwork_logging
