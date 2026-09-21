// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/units/magnitude.hh"
#include "jewels/units/meters.hh"
#include "jewels/units/miles.hh"
#include "jewels/units/quantity.hh"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <limits>

namespace au::testing
{
TEST_CASE("Smoke test kilometers, meters, and miles")
{
  constexpr auto some_value = meters(1000.0);
  CHECK(some_value.in(meters) == 1000.0);
  CHECK_THAT(some_value.in(miles), Catch::Matchers::WithinAbs(0.62137, std::numeric_limits<float>::epsilon() * 11));
  CHECK_THAT(some_value.in(kilometers), Catch::Matchers::WithinAbs(1, std::numeric_limits<float>::epsilon()));
}

TEST_CASE("Test UDL for meters")
{
  CHECK(1000_meters == meters(1000));
}

TEST_CASE("Test UDL for miles")
{
  CHECK(1000_miles == miles(1000));
}
} // namespace au::testing
