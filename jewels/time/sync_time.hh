// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <fmt10/core.h>

#include <chrono>
#include <cstdint>

namespace jewels::time
{

// Generally used to represent time points in C++. See guidance for more details
using SyncClock = std::chrono::system_clock;
using SyncTime = std::chrono::time_point<SyncClock>;

// Only use this for profiling code. See guidance for more details
using SteadyClock = std::chrono::steady_clock;
using SteadyTime = std::chrono::time_point<SteadyClock>;

} // namespace jewels::time

/// Allow SyncTime to be used in calls to format, log error messages, etc
template <>
struct fmt::formatter<jewels::time::SyncTime> : fmt::formatter<std::int64_t>
{
  auto format(const jewels::time::SyncTime& time, fmt::format_context& ctx) const
  {
    // SyncTime to nanoseconds conversion duplicated from conversions.hh to avoid cyclical dependencies.
    return fmt::formatter<std::int64_t>::format(
      std::chrono::duration_cast<std::chrono::nanoseconds>(time.time_since_epoch()).count(), ctx);
  }
};
