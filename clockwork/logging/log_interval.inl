// IWYU pragma: private, include "clockwork/logging/log_interval.hh"
#pragma once

#include "clockwork/logging/log_interval.hh"

#include "clockwork/logging/log_timestamp.hh"
#include "jewels/time/sync_time.hh"

#include <algorithm>
#include <chrono>
#include <compare>
#include <ostream>

namespace clockwork_logging
{

constexpr LogInterval::LogInterval(const LogTimestamp start_end_timestamp) noexcept
  : start_timestamp_(start_end_timestamp), end_timestamp_(start_end_timestamp)
{
}

constexpr LogInterval::LogInterval(const LogTimestamp start_timestamp, const LogTimestamp end_timestamp) noexcept
  : start_timestamp_(std::min(start_timestamp, end_timestamp)), end_timestamp_(std::max(start_timestamp, end_timestamp))
{
}

constexpr LogInterval::LogInterval(const jewels::time::SyncTime start_end_time) noexcept
  : start_timestamp_(start_end_time), end_timestamp_(start_end_time)
{
}

constexpr LogInterval::LogInterval(
  const jewels::time::SyncTime start_time, const jewels::time::SyncTime end_time) noexcept
  : start_timestamp_(std::min(start_time, end_time)), end_timestamp_(std::max(start_time, end_time))
{
}

[[nodiscard]] constexpr LogTimestamp LogInterval::get_start_timestamp() const noexcept
{
  return start_timestamp_;
}

[[nodiscard]] constexpr LogTimestamp LogInterval::get_end_timestamp() const noexcept
{
  return end_timestamp_;
}

[[nodiscard]] constexpr jewels::time::SyncTime LogInterval::get_start_time() const noexcept
{
  return start_timestamp_.get_time();
}

[[nodiscard]] constexpr jewels::time::SyncTime LogInterval::get_end_time() const noexcept
{
  return end_timestamp_.get_time();
}

[[nodiscard]] constexpr std::chrono::nanoseconds LogInterval::get_duration() const noexcept
{
  return end_timestamp_ - start_timestamp_;
}

constexpr void LogInterval::add_timestamp(const LogTimestamp timestamp) noexcept
{
  start_timestamp_ = std::min(start_timestamp_, timestamp);
  end_timestamp_ = std::max(end_timestamp_, timestamp);
}

constexpr void LogInterval::add_time(const jewels::time::SyncTime time) noexcept
{
  start_timestamp_ = std::min(start_timestamp_, LogTimestamp{time});
  end_timestamp_ = std::max(end_timestamp_, LogTimestamp{time});
}

constexpr void LogInterval::add_interval(const LogInterval& interval) noexcept
{
  start_timestamp_ = std::min(start_timestamp_, interval.start_timestamp_);
  end_timestamp_ = std::max(end_timestamp_, interval.end_timestamp_);
}

[[nodiscard]] constexpr bool LogInterval::contains(const LogTimestamp timestamp) const noexcept
{
  return (timestamp >= start_timestamp_) && (timestamp <= end_timestamp_);
}

[[nodiscard]] constexpr bool LogInterval::contains(const jewels::time::SyncTime time) const noexcept
{
  return (time >= start_timestamp_.get_time()) && (time <= end_timestamp_.get_time());
}

[[nodiscard]] constexpr bool LogInterval::overlaps(const LogInterval& interval) const noexcept
{
  return (start_timestamp_ <= interval.end_timestamp_) && (end_timestamp_ >= interval.start_timestamp_);
}

inline std::ostream& operator<<(std::ostream& ostream, const LogInterval& interval)
{
  ostream << "{" << interval.get_start_timestamp() << ", " << interval.get_end_timestamp() << "}";
  return ostream;
}

} // namespace clockwork_logging
