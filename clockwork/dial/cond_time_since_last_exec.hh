// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/dial/exec_condition.hh"

#include <chrono>
#include <cstdint>

namespace clockwork
{

/// Dial member for time_since_last_exec execution conditions.
///
/// This is meant to be a member of the Conditions part of a Cog Dial if (and
/// only if) the Cog has one or more time_since_last_exec execution conditions.
/// It provides information to the Cog function about whether the condition is
/// active and how much time has passed since the last execution.
///
/// @tparam threshold_ns: The threshold for the condition in nanoseconds.
template <uint64_t threshold_ns>
class TimeSinceLastExecCondition : public ExecCondition
{
public:
  /// Default constructor
  /// The default constructor leaves the condition inactive with a duration of 0.
  constexpr TimeSinceLastExecCondition() noexcept = default;

  /// Construct with explicit fields
  constexpr explicit TimeSinceLastExecCondition(bool is_active, std::chrono::nanoseconds time_since_last_exec) noexcept;

  /// Get the time since last execution of the cog
  ///
  /// Note: This is always from start time to start time.  The end time of the
  /// last execution is irrelevant, and the current time when this method is
  /// called is irrelevant.
  [[nodiscard]] constexpr std::chrono::nanoseconds get_time_since_last_exec() const noexcept;

  /// Get the threshold duration for this condition
  [[nodiscard]] consteval std::chrono::nanoseconds get_threshold() const noexcept;

private:
  std::chrono::nanoseconds time_since_last_exec_{};
};

} // namespace clockwork

#include "clockwork/dial/cond_time_since_last_exec.inl"
