// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/callsig/outcome.hh"
#include "jewels/time/sync_time.hh"

namespace clockwork::aligner
{

/// Control interface for the aligner's dynamic optional-input timeout timer.
///
/// Provides mutable access to the underlying timer handler for arming/disarming
/// during execute().
///
/// @tparam HandlerType The concrete DynamicTimerHandler<Policy> type
template <typename HandlerType>
class AlignerTimerControl
{
public:
  /// Construct from a handler reference.
  /// @param[in] handler Mutable reference to the timer handler
  explicit AlignerTimerControl(HandlerType& handler) noexcept;

  /// Arm the timer to fire at an absolute time (one-shot).
  /// After the timer fires, the cog will be scheduled for execution.
  /// @param[in] trigger_at The absolute SyncTime when the timer should fire
  jewels::BinaryOutcome arm(jewels::time::SyncTime trigger_at) const;

  /// Disarm the timer (cancel pending fire).
  /// Call this when optional inputs arrive before the deadline.
  jewels::BinaryOutcome disarm() const;

  /// Check if the timer is currently armed (waiting to fire).
  [[nodiscard]] bool is_armed() const;

private:
  HandlerType& handler_;
};

} // namespace clockwork::aligner

#include "clockwork/aligner/timer_control.inl"
