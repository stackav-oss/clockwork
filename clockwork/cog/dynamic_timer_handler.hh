// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/abstract_timer.hh"
#include "clockwork/dial/cond_dynamic_timer.hh"
#include "clockwork/pinion/observer.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/time/sync_time.hh"

#include <memory>
#include <mutex>

namespace clockwork
{

/// Handler for a dynamic one-shot timer.
///
/// Unlike TimeSinceLastExecHandler which manages periodic timers with a fixed
/// threshold, this handler supports dynamically arming a one-shot timer at an
/// arbitrary absolute time. The timer starts disarmed and is armed/disarmed
/// at runtime.
///
/// All public methods acquire the internal mutex before accessing mutable
/// state. The mutex is required because notify() is called from the timer/runner
/// thread while arm()/disarm()/make_condition() are called from the cog
/// execution thread.
///
/// @tparam Policy structure as follows:
///   struct Policy
///   {
///     // The handler type (must be DynamicTimerHandler<Policy>)
///     using HandlerType = DynamicTimerHandler<Policy>;
///     // The id of the timer endpoint
///     static constexpr auto endpoint_id = ...;
///     // Human-readable name for validation error messages
///     static constexpr std::string_view name = ...;
///   };
template <typename Policy>
class DynamicTimerHandler : public pinion::Observer
{
public:
  static constexpr auto endpoint_id = Policy::endpoint_id;
  using ConditionType = DynamicTimerCondition;

  /// Constructor.
  /// @param[in] timer The underlying abstract timer
  /// @param[in] cog_notify Observer to forward timer fire notifications to the cog
  explicit DynamicTimerHandler(std::shared_ptr<AbstractTimer> timer, std::shared_ptr<pinion::Observer> cog_notify);

  /// Notification from timer object when the timer fires.
  /// Called when the timer fires. Records the event and forwards
  /// the notification to the cog for scheduling.
  /// @param[in] event The timer event
  void notify(const Event& event) final;

  /// Construct the condition for the timer (read-only snapshot of fired state).
  /// @param[in] now The current time (unused, present for interface compatibility).
  /// @return The dynamic timer condition
  [[nodiscard]] ConditionType make_condition(jewels::time::SyncTime now) const;

  /// Clear the triggered flag after cog execution, but only if the timer
  /// condition was active during this execution cycle. This preserves the
  /// triggered state if the timer fires during an execution triggered by
  /// a different condition, preventing a missed fire.
  ///
  /// @param[in] last_exec_time The time of the start of the last execution (unused)
  /// @param[in] was_active Whether the timer condition was active during this execution
  /// @return true always (clearing triggered cannot fail)
  [[nodiscard]] bool update_last_exec_time(jewels::time::SyncTime last_exec_time, bool was_active);

  /// Arm the timer to fire at an absolute time (one-shot).
  /// @param[in] trigger_at The absolute SyncTime when the timer should fire
  jewels::BinaryOutcome arm(jewels::time::SyncTime trigger_at);

  /// Disarm the timer (cancel pending fire).
  jewels::BinaryOutcome disarm();

  /// Check if the timer is currently armed.
  [[nodiscard]] bool is_armed() const;

  /// Check if the timer has fired since last cleared.
  [[nodiscard]] bool has_fired() const;

  /// Invoke notify if the current time has reached the trigger time and
  /// the timer is armed.
  ///
  /// @param[in] now Current time
  void notify_if_triggered(jewels::time::SyncTime now);

private:
  /// Update internal state to triggered. Caller must hold mutex_.
  void set_triggered(const std::scoped_lock<std::mutex>& lock);

  /// Forward notification to the cog observer (must be called WITHOUT mutex_ held).
  void notify_observer(const Event& event);

  /// Mutex for thread-safe access to mutable state.
  mutable std::mutex mutex_;
  /// The underlying timer.
  std::shared_ptr<AbstractTimer> timer_;
  /// Observer to forward notifications to the cog.
  std::shared_ptr<pinion::Observer> cog_notify_;
  /// True when the timer has fired and not yet been cleared.
  bool triggered_{false};
  /// True when the timer is armed and hasn't fired yet.
  bool armed_{false};
  /// The absolute time at which the timer should fire (valid when armed).
  jewels::time::SyncTime trigger_at_{};
};

} // namespace clockwork

#include "clockwork/cog/dynamic_timer_handler.inl"
