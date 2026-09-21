// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/units/gps_time.hh"
#include "jewels/units/hours.hh"
#include "jewels/units/io.hh" // IWYU pragma: keep - keep this header so that Catch assertion failures are formatted
#include "jewels/units/magnitude.hh"
#include "jewels/units/minutes.hh"
#include "jewels/units/quantity.hh"
#include "jewels/units/seconds.hh"

#include <au/utility/string_constant.hh>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <ratio>
#include <thread>
#include <type_traits>

namespace au::testing
{
TEST_CASE("Smoke test minutes, hours, and seconds")
{
  constexpr auto minute_value = minutes(1);
  constexpr auto hour_value = hours(1);
  CHECK(minute_value.in(minutes) == 1);
  CHECK(minute_value.in(seconds) == 60);
  CHECK(hour_value.in(hours) == 1);
  CHECK(hour_value.in(minutes) == 60);
}

TEST_CASE("Test UDL for nanoseconds")
{
  CHECK(1000_nanoseconds == nanoseconds(1000));
}

TEST_CASE("Test UDL for milliseconds")
{
  CHECK(1000_milliseconds == milliseconds(1000));
}

TEST_CASE("Test UDL for seconds")
{
  CHECK(1000_seconds == seconds(1000));
}

TEST_CASE("Test UDL for minutes")
{
  CHECK(1000_minutes == minutes(1000));
}

TEST_CASE("Test UDL for hours")
{
  CHECK(1000_hours == hours(1000));
}

TEST_CASE("Test implicit conversion from chrono")
{
  using Clock = std::chrono::high_resolution_clock;
  using TimePoint = std::chrono::time_point<Clock>;

  // Check that std::chrono::high_resolution_clock's durations are implicitly convertable to Au types.
  constexpr TimePoint time_point_1;
  constexpr TimePoint time_point_2 = time_point_1 + std::chrono::minutes{1};
  constexpr Clock::duration std_duration = time_point_2 - time_point_1;
  constexpr QuantityI64<Nanoseconds> au_duration = std_duration;
  CHECK(au_duration == std_duration);
  CHECK(au_duration == std::chrono::minutes{1});
  CHECK(au_duration == minutes(1));

  /// Check that durations are implicitly convertable from Au types to std::chrono.
  CHECK(1000_hours == std::chrono::hours{1000});
  CHECK(1000_minutes == std::chrono::minutes{1000});
  CHECK(1000_seconds == std::chrono::seconds{1000});

  // Check that standard library functions are callable using Au's time types.
  constexpr auto sleep_for_seconds = std::this_thread::sleep_for<int32_t, std::ratio<1>>;
  (void)sleep_for_seconds;
  static_assert(std::is_invocable_v<decltype(sleep_for_seconds), QuantityI32<Seconds>>);
}

TEST_CASE("Test converting gps time")
{
  constexpr auto gps_week{1472};
  constexpr auto time_in_week_ms = std::chrono::milliseconds(314158000);
  CHECK(gps_time_to_utc(gps_week, time_in_week_ms) == 1206544540_seconds);

  constexpr auto gps_epoch_time = 1435068101000000000_nanoseconds;
  constexpr auto leap_seconds = 18_seconds;
  constexpr auto unix_time = 1751032883_seconds;
  CHECK(gps_time_to_utc(gps_epoch_time, leap_seconds) == unix_time);
}
} // namespace au::testing
