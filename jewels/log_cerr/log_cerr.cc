// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/log_cerr/log_cerr.hh"

#include "jewels/log_cerr/detect_log_color_mode.hh"
#include "jewels/log_cerr/detect_log_threshold.hh"
#include "jewels/log_cerr/log_time.hh"

#include <compare>
#include <cstdint>

namespace jewels
{

namespace
{
// These are intentional globally cached checks and state.
// NOLINTNEXTLINE(fuchsia-statically-constructed-objects)
const DetectLogColorMode global_detect_log_color_mode;

// NOLINTNEXTLINE(fuchsia-statically-constructed-objects)
const DetectLogThreshold global_log_threshold;

// NOLINTNEXTLINE(fuchsia-statically-constructed-objects, cppcoreguidelines-avoid-non-const-global-variables)
LogClockPtr global_log_time_clock_ptr;

/// Get the current system time since the epoch, in nanoseconds.
int64_t system_clock_now_ns()
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(jewels::time::SyncClock::now().time_since_epoch())
    .count();
}

} // namespace

namespace impl
{

bool should_print_in_color()
{
  return global_detect_log_color_mode.should_use_color();
}

LogLevel get_log_threshold()
{
  return global_log_threshold.value();
}

EpochTime get_log_time()
{
  int64_t now_ns = 0;
  if (global_log_time_clock_ptr)
  {
    now_ns = global_log_time_clock_ptr->now_ns();
  }
  else
  {
    now_ns = system_clock_now_ns();
  }
  return EpochTime(now_ns);
}

void set_log_time_clock(const ::jewels::LogClockPtr& log_clock)
{
  // Implemented here to access file-scoped global_log_time_clock
  global_log_time_clock_ptr = log_clock;
}

} // namespace impl

LogCerrThrottle::LogCerrThrottle(std::chrono::nanoseconds min_interval)
  : min_interval_(min_interval)
{
}

[[nodiscard]] bool LogCerrThrottle::should_log()
{
  const auto now = time::SteadyClock::now();
  if (now - last_log_time_ >= min_interval_)
  {
    last_log_time_ = now;
    return true;
  }
  return false;
}

} // namespace jewels
