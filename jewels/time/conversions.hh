// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/time/sync_time.hh"
#include "jewels/units/seconds.hh"

#include <au/magnitude.hh>
#include <au/quantity.hh>

#include <chrono>
#include <cstdint>

namespace jewels::time
{

/// File for generally useful functions for converting between SyncTime and other types. Kept separate from sync_time.hh
/// to decrease sync_time's non-essential dependencies.

/// Convert @p time_diff from integer nanoseconds to au::SecondsD.
/// Specifically not overloaded with floating point type to avoid silent loss of precision.
/// You can explicitly cast this to au::SecondsF if desired.
[[nodiscard]] static constexpr au::SecondsD duration_ns_to_au_s(const std::chrono::nanoseconds time_diff)
{
  return au::SecondsD{time_diff};
}

/// Convert @p time_diff from aurora units au::SecondsD to chrono::nanoseconds.
[[nodiscard]] static constexpr std::chrono::nanoseconds au_s_to_duration_ns(const au::SecondsD time_diff)
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<double>(time_diff.in(au::seconds)));
}

/// Convert @p time_diff from integer nanoseconds to floating point seconds
/// @tparam NumType Numeric type of the result (typically float or double)
/// This should only be used in instances where you can't use the stronger types.
template <typename NumType>
[[nodiscard]] constexpr NumType to_seconds(const std::chrono::nanoseconds time_diff)
{
  return au::Quantity<au::Seconds, NumType>{time_diff}.in(au::Seconds{});
}

/// Return a SyncTime created from the corresponding @p time_ns.
/// This should only be used if you are reading in a SyncTime from something that isn't a stronger time type.
[[nodiscard]] inline SyncTime sync_time_from_ns(const int64_t time_ns)
{
  return SyncTime(std::chrono::nanoseconds(time_ns));
}

/// Return a SyncTime created from the corresponding @p time_ms.
/// This should only be used if you are reading in a SyncTime from something that isn't a stronger time type.
[[nodiscard]] inline SyncTime sync_time_from_ms(const int64_t time_ms)
{
  return SyncTime(std::chrono::milliseconds(time_ms));
}

/// Get the time in ns from a time point. Only use this when you need to save a SyncTime into an object that doesn't
/// support the SyncTime type, like a message to be published.
template <class TimeType>
[[nodiscard]] constexpr int64_t get_ns(TimeType time)
{
  // Cast to nanoseconds in case the underlying clock isn't using them already.
  return std::chrono::duration_cast<std::chrono::nanoseconds>(time.time_since_epoch()).count();
}

} // namespace jewels::time
