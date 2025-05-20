// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/time/sync_time.hh"

#include <chrono>
#include <compare>
#include <cstdint>
#include <ostream>

namespace clockwork_logging
{

/// Log timestamp
class LogTimestamp
{
public:
  LogTimestamp() noexcept = default;
  ~LogTimestamp() noexcept = default;
  LogTimestamp(const LogTimestamp&) noexcept = default;
  LogTimestamp& operator=(const LogTimestamp&) noexcept = default;
  LogTimestamp(LogTimestamp&&) noexcept = default;
  LogTimestamp& operator=(LogTimestamp&&) noexcept = default;

  /// Construct a log timestamp from a time value
  /// @param[in] time Time value
  explicit constexpr LogTimestamp(jewels::time::SyncTime time) noexcept;

  /// Construct a log timestamp from the number of nanoseconds since the start of the epoch
  /// @param[in] time_ns Nanoseconds since the start of the epoch
  explicit constexpr LogTimestamp(int64_t time_ns) noexcept;

  /// Construct a log timestamp from the duration since the start of the epoch
  /// @param[in] duration Duration since the start of the epoch
  explicit constexpr LogTimestamp(std::chrono::nanoseconds duration) noexcept;

  /// Get the time value
  /// @return Time value
  [[nodiscard]] constexpr jewels::time::SyncTime get_time() const noexcept;

  /// Get the duration since the start of the epoch
  /// @return Duration since the start of the epoch
  [[nodiscard]] constexpr std::chrono::nanoseconds get_duration() const noexcept;

  /// Get the number of nanoseconds since the start of the epoch
  /// @return Nanoseconds since the start of the epoch
  [[nodiscard]] constexpr int64_t get_nanoseconds() const noexcept;

  /// += operator
  /// @param[in] rhs Right hand side
  /// @return *this + rhs
  constexpr LogTimestamp operator+=(std::chrono::nanoseconds rhs) noexcept;

  /// Addition operator
  /// @param[in] lhs Left hand side
  /// @param[in] rhs Right hand side
  /// @return lhs + rhs
  friend constexpr LogTimestamp operator+(LogTimestamp lhs, std::chrono::nanoseconds rhs) noexcept
  {
    return LogTimestamp(lhs.time_ + rhs);
  }

  /// -= operator
  /// @param[in] rhs Right hand side
  /// @return *this + rhs
  constexpr LogTimestamp operator-=(std::chrono::nanoseconds rhs) noexcept;

  /// Subtraction operator
  /// @param[in] lhs Left hand side
  /// @param[in] rhs Right hand side
  /// @return lhs - rhs
  friend constexpr LogTimestamp operator-(LogTimestamp lhs, std::chrono::nanoseconds rhs) noexcept
  {
    return LogTimestamp(lhs.time_ - rhs);
  }

  /// Subtraction operator
  /// @param[in] lhs Left hand side
  /// @param[in] rhs Right hand side
  /// @return lhs - rhs
  friend constexpr std::chrono::nanoseconds operator-(LogTimestamp lhs, LogTimestamp rhs) noexcept
  {
    return lhs.time_ - rhs.time_;
  }

  /// == comparison operator
  /// @param[in] lhs Left hand side
  /// @param[in] rhs Right hand side
  /// @return True if lhs == rhs
  friend constexpr bool operator==(LogTimestamp lhs, LogTimestamp rhs) noexcept
  {
    return lhs.time_ == rhs.time_;
  }

  /// != comparison operator
  /// @param[in] lhs Left hand side
  /// @param[in] rhs Right hand side
  /// @return True if lhs != rhs
  friend constexpr bool operator!=(LogTimestamp lhs, LogTimestamp rhs) noexcept
  {
    return lhs.time_ != rhs.time_;
  }

  /// < comparison operator
  /// @param[in] lhs Left hand side
  /// @param[in] rhs Right hand side
  /// @return True if lhs < rhs
  friend constexpr bool operator<(LogTimestamp lhs, LogTimestamp rhs) noexcept
  {
    return lhs.time_ < rhs.time_;
  }

  /// > comparison operator
  /// @param[in] lhs Left hand side
  /// @param[in] rhs Right hand side
  /// @return True if lhs > rhs
  friend constexpr bool operator>(LogTimestamp lhs, LogTimestamp rhs) noexcept
  {
    return lhs.time_ > rhs.time_;
  }

  /// <= comparison operator
  /// @param[in] lhs Left hand side
  /// @param[in] rhs Right hand side
  /// @return True if lhs <= rhs
  friend constexpr bool operator<=(LogTimestamp lhs, LogTimestamp rhs) noexcept
  {
    return lhs.time_ <= rhs.time_;
  }

  /// >= comparison operator
  /// @param[in] lhs Left hand side
  /// @param[in] rhs Right hand side
  /// @return True if lhs >= rhs
  friend constexpr bool operator>=(LogTimestamp lhs, LogTimestamp rhs) noexcept
  {
    return lhs.time_ >= rhs.time_;
  }

private:
  /// Time value
  jewels::time::SyncTime time_{};
};

/// Output stream insertion operator for log timestamps
/// @param[in] ostream Output stream
/// @param[in] timestamp Log timestamp
/// @return Output stream reference
inline std::ostream& operator<<(std::ostream& ostream, LogTimestamp timestamp);

} // namespace clockwork_logging

#include "clockwork/logging/log_timestamp.inl"
