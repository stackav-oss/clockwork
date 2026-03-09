// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/units/io.hh" // IWYU pragma: keep
#include "jewels/units/meters.hh"
#include "jewels/units/quantity.hh"

#include <au/magnitude.hh>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <limits>

namespace au::testing
{
TEST_CASE("Test integer UDL for meters")
{
  CHECK(1000_meters == meters(1000));
  CHECK(42_meters == meters(42));
  CHECK(50_m == meters(50));

  CHECK(0_meters == meters(0));
  CHECK(0_m == meters(0));

  CHECK(-100_meters == meters(-100));
  CHECK(-50_m == meters(-50));

  constexpr auto compile_time_int = 100_meters;
  CHECK(compile_time_int.in(meters) == 100);
}

TEST_CASE("Test floating-point UDL for meters")
{
  CHECK(1000.5_meters == meters(1000.5));
  CHECK(42.7_meters == meters(42.7));
  CHECK(50.25_m == meters(50.25));

  CHECK(0.0_meters == meters(0.0));
  CHECK(0.0_m == meters(0.0));

  CHECK(-42.5_meters == meters(-42.5));
  CHECK(-50.75_m == meters(-50.75));

  // Floating-point UDL is constexpr
  constexpr auto compile_time_float = 100.5_meters;
  CHECK(compile_time_float.in(meters) == 100.5);
}

TEST_CASE("Test large values with meters UDL")
{
  constexpr auto large_int = 1000000_meters;
  CHECK(large_int == meters(1000000));

  constexpr auto large_float = 123456.789_meters;
  CHECK(large_float == meters(123456.789));
}

TEST_CASE("Test meters UDL with conversions")
{
  constexpr auto one_km = 1000_meters;
  // Note that we have to provide the explicit template parameter to allow this to indicate an explicit cast to double
  CHECK_THAT(one_km.in<double>(kilometers), Catch::Matchers::WithinAbs(1.0, std::numeric_limits<double>::epsilon()));
  CHECK_THAT(one_km.in<float>(kilometers), Catch::Matchers::WithinAbs(1.0f, std::numeric_limits<float>::epsilon()));

  // Test that the UDL defined value can be implicitly cast to a float, and still contains the correct value.
  constexpr au::MetersF one_km_f = one_km;
  CHECK_THAT(one_km_f.in(kilometers), Catch::Matchers::WithinAbs(1.0f, std::numeric_limits<float>::epsilon()));

  constexpr auto half_km = 500.0_meters;
  CHECK_THAT(half_km.in(kilometers), Catch::Matchers::WithinAbs(0.5, std::numeric_limits<double>::epsilon()));
}

TEST_CASE("Test mixed integer and floating-point meters UDL")
{
  constexpr auto mixed = 10_meters + 5.5_meters;
  CHECK_THAT(mixed.in(meters), Catch::Matchers::WithinAbs(15.5, std::numeric_limits<double>::epsilon()));
}

TEST_CASE("Test meters UDL value preservation")
{
  constexpr auto int_value = 9'000'000'000'000'000'000_meters;
  CHECK(int_value.in(meters) == 9'000'000'000'000'000'000);

  constexpr auto float_value = 123456.789_meters;
  CHECK_THAT(float_value.in(meters), Catch::Matchers::WithinAbs(123456.789, std::numeric_limits<double>::epsilon()));
}

TEST_CASE("Test meters UDL comparison operations")
{
  CHECK(100_meters > 50_meters);
  CHECK(50_meters < 100_meters);
  CHECK(100_meters >= 100_meters);
  CHECK(100_meters <= 100_meters);
  CHECK(100_meters == 100_meters);
  CHECK(100_meters != 99_meters);

  CHECK(100.5_meters > 100.0_meters);
  CHECK(100.0_meters < 100.5_meters);
}
} // namespace au::testing
