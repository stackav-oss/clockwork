// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dsl/tests/support/tapmsg.hh"

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

namespace
{

TEST_CASE("ctor", "[tapmsg]")
{
  REQUIRE_NOTHROW(clockwork::Tap<clockwork::Tachyon<::clockwork::testing::SubMsg>>());
  REQUIRE_NOTHROW(clockwork::Tap<clockwork::Tachyon<::clockwork::testing::TapMsg<234>>>());
  REQUIRE_NOTHROW(clockwork::Tap<clockwork::Tachyon<::clockwork::testing::TapMsg<>>>());
  STATIC_REQUIRE(clockwork::Tap<clockwork::Tachyon<::clockwork::testing::TapMsg<>>>::signed_value == 234);
  STATIC_REQUIRE(clockwork::Tap<clockwork::Tachyon<::clockwork::testing::GenericMsg<>>>::value_par == 3);
  STATIC_REQUIRE(
    std::is_same_v<
      clockwork::Tachyon<::clockwork::testing::GenericMsg<>>::TypePar,
      clockwork::Tappy<clockwork::testing::TapMsg<>>>);
  STATIC_REQUIRE(
    std::is_same_v<
      clockwork::Tap<clockwork::Tachyon<::clockwork::testing::GenericMsg<>>>::TypePar,
      clockwork::Tappy<clockwork::testing::TapMsg<>>>);
}

TEST_CASE("optional", "[tapmsg]")
{
  constexpr uint32_t value = 5;
  constexpr int32_t signed_value = 234;
  clockwork::Tap<clockwork::Tachyon<::clockwork::testing::TapMsg<signed_value>>> msg;
  const auto& cmsg = msg;

  CHECK_FALSE(msg.has_optional());
  CHECK_FALSE(msg.get_underlying_optional().has_value());
  msg.set_optional(value);
  REQUIRE(cmsg.has_optional());
  CHECK(cmsg.value_optional() == value);
  CHECK(cmsg.get_underlying_optional().has_value());
  CHECK(cmsg.get_underlying_optional().value() == value);
  CHECK(msg.value_mutable_optional() == value);
  msg.reset_optional();
  CHECK_FALSE(cmsg.has_optional());
  CHECK_FALSE(cmsg.get_underlying_optional().has_value());
  CHECK(cmsg.signed_value == signed_value);
}

} // namespace
