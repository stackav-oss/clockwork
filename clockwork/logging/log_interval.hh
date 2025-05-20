// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_timestamp.hh"
#include "jewels/time/sync_time.hh"

#include <chrono>
#include <compare>
#include <ostream>
#include <tuple>

namespace clockwork_logging
{

/// Log interval containing time such that time >= start_timestamp and time <= end_timestamp
class LogInterval
{
public:
  /// Construct a log interval containing a single timestamp
  /// @param[in] start_end_timestamp Start and end timestamp
  explicit constexpr LogInterval(LogTimestamp start_end_timestamp) noexcept;

  /// Construct a log interval from start and end timestamps
  /// The start and end times are switched if start time is greater than end time
  /// @param[in] start_timestamp Start timestamp
  /// @param[in] end_timestamp End timestamp
  constexpr LogInterval(LogTimestamp start_timestamp, LogTimestamp end_timestamp) noexcept;

  /// Construct a log interval containing a single time
  /// @param[in] start_end_time Start and end time
  explicit constexpr LogInterval(jewels::time::SyncTime start_end_time) noexcept;

  /// Construct a log interval from start and end timestamps
  /// The start and end times are switched if start time is greater than end time
  /// @param[in] start_time Start time
  /// @param[in] end_time End time
  constexpr LogInterval(jewels::time::SyncTime start_time, jewels::time::SyncTime end_time) noexcept;

  /// Default constructor
  LogInterval() noexcept = default;

  ~LogInterval() noexcept = default;
  LogInterval(const LogInterval&) noexcept = default;
  LogInterval& operator=(const LogInterval&) noexcept = default;
  LogInterval(LogInterval&&) noexcept = default;
  LogInterval& operator=(LogInterval&&) noexcept = default;

  /// Get the interval start timestamp
  /// @return Start timestamp
  [[nodiscard]] constexpr LogTimestamp get_start_timestamp() const noexcept;

  /// Get the interval end timestamp
  /// @return End timestamp
  [[nodiscard]] constexpr LogTimestamp get_end_timestamp() const noexcept;

  /// Get the interval start time
  /// @return Start time
  [[nodiscard]] constexpr jewels::time::SyncTime get_start_time() const noexcept;

  /// Get the interval end time
  /// @return End time
  [[nodiscard]] constexpr jewels::time::SyncTime get_end_time() const noexcept;

  /// Get the interval duration
  /// @return Interval duration
  [[nodiscard]] constexpr std::chrono::nanoseconds get_duration() const noexcept;

  /// Expand the interval to cover a timestamp
  /// @param[in] timestamp Timestamp to add to this interval
  constexpr void add_timestamp(LogTimestamp timestamp) noexcept;

  /// Expand the interval to cover a time value
  /// @param[in] time Time to add to this interval
  constexpr void add_time(jewels::time::SyncTime time) noexcept;

  /// Expand the interval to cover an interval
  /// @param[in] interval Interval to add to this interval
  constexpr void add_interval(const LogInterval& interval) noexcept;

  /// Test whether this interval contains a timestamp, start and end times inclusive
  /// @param[in] timestamp Timestamp to check
  /// @return True iff this interval contains the timestamp
  [[nodiscard]] constexpr bool contains(LogTimestamp timestamp) const noexcept;

  /// Test whether this interval contains a time value, start and end times inclusive
  /// @param[in] time Time value to check
  /// @return True iff this interval contains the time
  [[nodiscard]] constexpr bool contains(jewels::time::SyncTime time) const noexcept;

  /// Test whether this interval overlaps another interval
  /// @param[in] interval Interval to check
  /// @return True iff the interval overlaps this interval
  [[nodiscard]] constexpr bool overlaps(const LogInterval& interval) const noexcept;

  /// Equality comparison operator
  /// @param[in] lhs Left hand operand
  /// @param[in] rhs Right hand operand
  /// @return True iff lhs == rhs
  [[nodiscard]] friend bool operator==(const LogInterval& lhs, const LogInterval& rhs) noexcept
  {
    return std::tie(lhs.start_timestamp_, lhs.end_timestamp_) == std::tie(rhs.start_timestamp_, rhs.end_timestamp_);
  }

  /// Inequality comparison operator
  /// @param[in] lhs Left hand operand
  /// @param[in] rhs Right hand operand
  /// @return True iff lhs != rhs
  [[nodiscard]] friend bool operator!=(const LogInterval& lhs, const LogInterval& rhs) noexcept
  {
    return !(lhs == rhs);
  }

  /// Less than comparison operator
  /// @param[in] lhs Left hand operand
  /// @param[in] rhs Right hand operand
  /// @return True iff lhs < rhs
  [[nodiscard]] friend bool operator<(const LogInterval& lhs, const LogInterval& rhs) noexcept
  {
    return std::tie(lhs.start_timestamp_, lhs.end_timestamp_) < std::tie(rhs.start_timestamp_, rhs.end_timestamp_);
  }

private:
  /// Start timestamp
  LogTimestamp start_timestamp_{};

  /// End timestamp
  LogTimestamp end_timestamp_{};
};

/// Output stream insertion operator for log intervals
/// @param[in] ostream Output stream
/// @param[in] interval Log interval
/// @return Output stream reference
inline std::ostream& operator<<(std::ostream& ostream, const LogInterval& interval);

} // namespace clockwork_logging

#include "clockwork/logging/log_interval.inl"
