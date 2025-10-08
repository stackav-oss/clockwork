// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/units/degrees.hh"
#include "jewels/units/magnitude.hh"
#include "jewels/units/quantity.hh"

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

namespace au::testing
{
TEST_CASE("Centidegrees")
{
  constexpr Quantity<CentiDegrees, double> value_cd{centidegrees(100.00)};
  REQUIRE(value_cd.in(degrees) == 1.0);

  auto value_cd_u16 = rep_cast<uint16_t>(value_cd);
  STATIC_REQUIRE(std::is_same_v<decltype(value_cd_u16), CentiDegreesU16>);
  REQUIRE(value_cd_u16.in(centidegrees) == 100U);
}
} // namespace au::testing
