// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/units/math.hh"
#include "jewels/units/meters.hh"

#include <au/magnitude.hh>
#include <au/quantity.hh>
#include <catch2/catch_test_macros.hpp>

namespace au::testing
{
TEST_CASE("Linearly interpolates scalar values")
{
  CHECK(lerp(10.0, 20.0, 0.25) == 12.5);
}

TEST_CASE("Linearly interpolates quantities")
{
  CHECK(lerp(meters(10.0), meters(20.0), 0.25) == meters(12.5));
}

TEST_CASE("Linearly interpolates quantities with different units")
{
  CHECK(lerp(meters(1000.0), kilometers(2.0), 0.5) == meters(1500.0));
}

TEST_CASE("Returns quantity endpoints for interpolation factors of zero and one")
{
  CHECK(lerp(meters(10.0), meters(20.0), 0.0) == meters(10.0));
  CHECK(lerp(meters(10.0), meters(20.0), 1.0) == meters(20.0));
}
} // namespace au::testing
