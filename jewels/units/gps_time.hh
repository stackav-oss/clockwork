// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstdint>

namespace au
{

/// The number of leap seconds that have occurred since the GPS epoch began.
constexpr auto leap_seconds_default = std::chrono::seconds(18);

/// Convert from gps time to utc time.
///
/// @param gps_week The number of weeks since the gps epoch began.
/// @param time_in_week_ms The amount time since the start of this week.
/// @return The equivalent UTC time.
std::chrono::nanoseconds gps_time_to_utc(
  uint16_t gps_week,
  std::chrono::milliseconds time_in_week_ms,
  std::chrono::seconds leap_seconds = leap_seconds_default);

/// Convert from gps time to utc time.
///
/// @param gps_epoch_time The time relative to the gps epoch.
/// @param gps_leap_seconds The number of leap seconds to apply.
/// @return The equivalent UTC time.
std::chrono::nanoseconds
gps_time_to_utc(std::chrono::nanoseconds gps_epoch_time, std::chrono::seconds gps_leap_seconds);

} // namespace au
