// IWYU pragma: private, include "clockwork/logging/log_timestamp.hh"
#pragma once

#include "clockwork/logging/log_timestamp.hh"

#include "jewels/time/sync_time.hh"

#include <chrono>
#include <cstdint>
#include <iosfwd>
#include <ostream>

namespace clockwork_logging
{

constexpr LogTimestamp::LogTimestamp(jewels::time::SyncTime time) noexcept
  : time_(time)
{
}

constexpr LogTimestamp::LogTimestamp(std::chrono::nanoseconds duration) noexcept
  : time_(duration)
{
}

constexpr LogTimestamp::LogTimestamp(int64_t time_ns) noexcept
  : time_(std::chrono::nanoseconds(time_ns))
{
}

[[nodiscard]] constexpr jewels::time::SyncTime LogTimestamp::get_time() const noexcept
{
  return time_;
}

[[nodiscard]] constexpr std::chrono::nanoseconds LogTimestamp::get_duration() const noexcept
{
  return time_.time_since_epoch();
}

[[nodiscard]] constexpr int64_t LogTimestamp::get_nanoseconds() const noexcept
{
  return time_.time_since_epoch().count();
}

constexpr LogTimestamp LogTimestamp::operator+=(std::chrono::nanoseconds rhs) noexcept
{
  time_ += rhs;
  return *this;
}

constexpr LogTimestamp LogTimestamp::operator-=(std::chrono::nanoseconds rhs) noexcept
{
  time_ -= rhs;
  return *this;
}

inline std::ostream& operator<<(std::ostream& ostream, const LogTimestamp timestamp)
{
  ostream << timestamp.get_nanoseconds();
  return ostream;
}

} // namespace clockwork_logging
