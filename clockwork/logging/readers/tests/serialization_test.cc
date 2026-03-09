// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/readers/serialization.hh"
#include "clockwork/logging/tests/support/test_message_clk_cc.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/container/tap/var_string.hh"

#include <catch2/catch_test_macros.hpp>

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

  auto deserialized = MsgType();
  deserialize_tachyon(deserialized, std::as_bytes(std::span(&original, 1U)));

  REQUIRE(original == deserialized);
}

} // namespace
} // namespace clockwork_logging
