// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/meta/functional.hh"
#include "jewels/meta/types.hh"

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

namespace jewels::meta
{
TEST_CASE("KeepIf")
{
  using TestTypes = Types<int, bool, float, double>;
  using IntegralTypes = KeepIf<std::is_integral, TestTypes>::type;

  STATIC_REQUIRE(std::is_same_v<IntegralTypes, Types<int, bool>>);
  STATIC_REQUIRE(size(IntegralTypes{}) == 2);

  using FloatingTypes = KeepIf<std::is_floating_point, IntegralTypes>::type;
  STATIC_REQUIRE(std::is_same_v<FloatingTypes, Types<>>);
  STATIC_REQUIRE(size(FloatingTypes{}) == 0);
}

} // namespace jewels::meta
