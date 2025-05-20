// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstdint>

namespace au
{

/// Convert from gps time to utc time.
///
/// @param gps_week The number of weeks since the gps epoch began.
/// @param time_in_week_ms The amount time since the start of this week.
/// @return The equivalent UTC time.
std::chrono::nanoseconds gps_time_to_utc(uint16_t gps_week, std::chrono::milliseconds time_in_week_ms);

} // namespace au
