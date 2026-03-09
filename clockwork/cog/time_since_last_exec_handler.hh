// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/abstract_timer.hh"
#include "clockwork/dial/cond_time_since_last_exec.hh"
#include "clockwork/pinion/observer.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"

#include <memory>
#include <mutex>

namespace clockwork
{

/// Helper class to handle time since last execution timer logic.
///
/// @tparam Policy structure as follows:
///   struct Policy
///   {
///     // The time since last execution threshold in nanoseconds.
///     static constexpr int64_t threshold_ns;
///     // The id of the timer
///     jewels::Uuid<common::EndpointClassId> timer_id;
///   };
template <typename Policy>
class TimeSinceLastExecHandler : public pinion::Observer
{
public:
  static constexpr auto threshold_ns = Policy::threshold_ns;
  static constexpr auto endpoint_id = Policy::endpoint_id;
  using ConditionType = TimeSinceLastExecCondition<threshold_ns>;

  /// Constructor.
  /// @param[in] timer The underlying timer
  explicit TimeSinceLastExecHandler(std::shared_ptr<AbstractTimer> timer, std::shared_ptr<pinion::Observer> cog_notify);

  /// Notification from timer object
  /// @param[in] event ignored
  void notify(const Event& event) override;

  /// Construct the condition for the timer.
  /// @param[in] now The current time.
  /// @return The time since last execution condition.
  [[nodiscard]] ConditionType make_condition(jewels::time::SyncTime now) const;

  /// State of condition
  struct Status
  {
    /// Last execution time
    jewels::time::SyncTime last_exec_time;
    /// Expected next trigger time
    jewels::time::SyncTime expected_next_trigger;

    bool operator==(const Status& other) const = default;
  };

  /// Update last executed time and restart the timer if needed.
  ///
  /// This only resets the timer if the condition was not active, because if it
  /// was active that indicates periodic scheduling, so that we should just let
  /// the timer re-arm on its original schedule.  No matter what, this clears
  /// the triggered flag.
  ///
  /// @param[in] last_exec_time The time of the start of the last execution.
  /// @param[in] was_active Determines if the condition triggered the last
  /// execution @return void if successful or unexpected if starting the timer fails
  [[nodiscard]] jewels::expected<Status, jewels::MonoError>
  update_last_exec_time(jewels::time::SyncTime last_exec_time, bool was_active);

  /// Invoke notify if the current time has reached the expected next trigger time
  ///
  /// Used by unit test cogs with a dummy timer
  ///
  /// @param[in] now Current time
  void notify_if_triggered(jewels::time::SyncTime now);

private:
  /// Mutex for updating internal state, because we could be called from multiple threads
  /// Mutable so that const methods can still obtain a lock before reading.
  mutable std::mutex mutex_;
  /// The underlying timer.
  std::shared_ptr<AbstractTimer> timer_;
  /// The cog to forward the notification too (since notify isn't part of abstract cog)
  std::shared_ptr<pinion::Observer> cog_notify_;
  /// Last execution time
  jewels::time::SyncTime last_exec_time_{};
  /// Expected next trigger time
  jewels::time::SyncTime expected_next_trigger_{};
  /// Indicates if the condition was notified by the timer since the last update of last_exec_time
  bool triggered_{false};
};

} // namespace clockwork

#include "clockwork/cog/time_since_last_exec_handler.inl"
