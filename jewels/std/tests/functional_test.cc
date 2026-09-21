// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/std/functional.hh"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <type_traits>

namespace jewels
{

TEST_CASE("identity")
{
  CHECK(Identity()(true) == true);
  CHECK(Identity()(-5) == -5);
  CHECK(Identity()(1ULL << 33U) == 1ULL << 33U);
  CHECK(Identity()(std::string{"foo"}) == std::string{"foo"});
  static_assert(std::is_rvalue_reference_v<decltype(Identity()(std::string{"foo"}))>);
  const std::string str{"a string"};
  static_assert(std::is_lvalue_reference_v<decltype(Identity()(str))>);
}

} // namespace jewels
