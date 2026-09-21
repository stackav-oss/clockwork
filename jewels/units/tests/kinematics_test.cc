// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/units/kinematics.hh"
#include "jewels/units/meters.hh"
#include "jewels/units/quantity.hh"
#include "jewels/units/seconds.hh"

#include <au/packs.hh>
#include <au/unit_of_measure.hh>
#include <catch2/catch_test_macros.hpp>

#include <type_traits>

namespace au::testing
{
TEST_CASE("Smoke test for quantities")
{
  SECTION("Velocity")
  {
    constexpr MetersPerSecondD some_value = mps(100.0);
    CHECK(some_value.in(mps) == 100.0);
    constexpr MetersPerSecondF some_value_f = mps(100.0f);
    CHECK(some_value_f.in(mps) == 100.0f);

    CHECK(some_value == au::meters(100.0) / au::seconds(1.0));
  }
  SECTION("Acceleration")
  {
    constexpr MetersPerSecondSquaredD some_value = mpss(100.0);
    CHECK(some_value.in(mpss) == 100.0);
    constexpr MetersPerSecondSquaredF some_value_f = mpss(100.0f);
    CHECK(some_value_f.in(mpss) == 100.0f);

    CHECK(some_value == au::meters(100.0) / (au::seconds(1.0) * au::seconds(1.0)));
  }
}

TEST_CASE("Test UDL for velocity")
{
  CHECK(24_mps == mps(24));
}

TEST_CASE("Test UDL for acceleration")
{
  CHECK(37_mpss == mpss(37));
}

TEST_CASE("Curvature units")
{
  constexpr OnePerMeterD value = one_per_meter(0.3);
  constexpr auto manual_value = 0.3 / meters(1.0);
  CHECK(value == manual_value);
}

TEST_CASE("Curvature rate units")
{
  constexpr OnePerMeterPerSecondD value = one_per_meter_per_second(0.3);
  constexpr auto manual_value = 0.3 / meters(1.0) / seconds(1.0);
  CHECK(value == manual_value);
}

TEST_CASE("RadiansPerNanosecond")
{
  const auto value = radians(4.0) / nanoseconds(2.0);
  REQUIRE(value == rads_per_nanosecond(2.0));
  STATIC_REQUIRE(au::AreQuantityTypesEquivalent<std::decay_t<decltype(value)>, RadsPerNanosecondD>::value);
}

TEST_CASE("NanosecondsPerRadian")
{
  const auto value = nanoseconds(4.0) / radians(2.0);
  REQUIRE(value == nanoseconds_per_rad(2.0));
  STATIC_REQUIRE(au::AreQuantityTypesEquivalent<std::decay_t<decltype(value)>, NanosecondsPerRadD>::value);
}

} // namespace au::testing
