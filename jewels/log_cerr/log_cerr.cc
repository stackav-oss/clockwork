// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/log_cerr/log_cerr.hh"

#include "jewels/log_cerr/detect_log_color_mode.hh"
#include "jewels/log_cerr/detect_log_threshold.hh"

#include <compare>

namespace jewels
{

namespace
{
// These are intentional globally cached checks.
// NOLINTNEXTLINE(fuchsia-statically-constructed-objects)
const DetectLogColorMode global_detect_log_color_mode;

// NOLINTNEXTLINE(fuchsia-statically-constructed-objects)
const DetectLogThreshold global_log_threshold;

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
