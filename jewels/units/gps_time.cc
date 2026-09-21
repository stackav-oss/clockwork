// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/units/gps_time.hh"

#include "jewels/units/hours.hh"
#include "jewels/units/magnitude.hh"
#include "jewels/units/quantity.hh"
#include "jewels/units/seconds.hh"

#include <au/unit_of_measure.hh>

namespace au
{

namespace
{
constexpr auto weeks_to_hours{7 * 24};

/// Delta between the GPS epoch (1980-01-06) and UTC time (1970-01-01)
constexpr auto epoch_delta = 315964800_seconds;

} // namespace

std::chrono::nanoseconds
gps_time_to_utc(uint16_t gps_week, std::chrono::milliseconds time_in_week_ms, std::chrono::seconds leap_seconds)
{
  // Convert the weeks to a time type... presently hours is the largest so we'll use that.
  const auto weeks = QuantityI64<Hours>(hours(weeks_to_hours * gps_week));
  // Now combine the gps times (total time relative to the gps epoch), add an
  // offset to get to the unix epoch, then account for the awesome leap seconds.
  return weeks + time_in_week_ms + epoch_delta - leap_seconds;
}

std::chrono::nanoseconds gps_time_to_utc(std::chrono::nanoseconds gps_epoch_time, std::chrono::seconds gps_leap_seconds)
{
  return gps_epoch_time + epoch_delta - gps_leap_seconds;
}

} // namespace au
