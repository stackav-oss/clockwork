#pragma once
// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/time/sync_time.hh"

#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

namespace jewels
{

/// Timestamp data for log messages
struct EpochTime
{
  int64_t seconds{0};
  int64_t nanoseconds{0};

  static constexpr int64_t seconds_per_ns{1'000'000'000};

  explicit EpochTime(int64_t epoch_nanoseconds)
    : seconds(epoch_nanoseconds / seconds_per_ns), nanoseconds(epoch_nanoseconds % seconds_per_ns)
  {
  }
};

/// Pure virtual class for defining a clock interface to be used for logging timestamps.
class LogClock
{
public:
  LogClock() = default;
  LogClock(const LogClock&) = delete;
  LogClock& operator=(const LogClock&) = delete;
  LogClock(LogClock&&) = delete;
  LogClock& operator=(LogClock&&) = delete;
  virtual ~LogClock() = default;
  /// Get the current time since the epoch in nanoseconds.
  [[nodiscard]] virtual int64_t now_ns() const = 0;
};
/// Alias for shared pointer to LogClock
using LogClockPtr = std::shared_ptr<LogClock>;

/// LogClock implementation for use in simulation
///
/// The simulated clock is expected to be advanced externally by setting the
/// sim_time_nanoseconds field to the current time in nanoseconds since the epoch.
class SimLogClock : public ::jewels::LogClock
{
public:
  [[nodiscard]] int64_t now_ns() const override
  {
    return sim_time_nanoseconds;
  }
  /// Current simulated time in nanoseconds
  int64_t sim_time_nanoseconds{0};
};

} // namespace jewels
