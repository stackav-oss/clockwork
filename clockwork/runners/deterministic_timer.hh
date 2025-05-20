// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/abstract_timer.hh"
#include "clockwork/pinion/observer.hh"
#include "jewels/time/sync_time.hh"

#include <chrono>
#include <cstdint>
#include <optional>

namespace clockwork
{

///
/// A simple timer. Can be used for both one-shot and periodic timers. It does not support use with epoll.
///
class DeterministicTimer : public AbstractTimer
{
public:
  DeterministicTimer();

  ~DeterministicTimer() override = default;

  DeterministicTimer(const DeterministicTimer&) = delete;
  DeterministicTimer& operator=(const DeterministicTimer&) = delete;
  DeterministicTimer(DeterministicTimer&&) = delete;
  DeterministicTimer& operator=(DeterministicTimer&&) = delete;

  ///
  /// Update the current time and fire the timer if necessary
  ///
  /// @param[in] now The current / stating timestamp
  ///
  void update(jewels::time::SyncTime now);

  ///
  /// Get the file descriptor.
  ///
  /// @return The file descriptor associated with this timer.
  ///
  [[nodiscard]] int32_t descriptor() const override;

  ///
  /// Set the observer to notify on events.
  ///
  void set_observer(pinion::Observer* observer) override;

  ///
  /// Start the timer. This will cancel any active timer.
  ///
  /// @param[in] trigger_at The next trigger time
  /// @param[in] period The period for auto re-triggering (or 0 for single-shot)
  /// @return true if start succeeded.
  ///
  [[nodiscard]] bool start(jewels::time::SyncTime trigger_at, std::chrono::nanoseconds period) override;

  ///
  /// Called when epoll indicates that the registered file descriptor has pending events
  /// @param epoll the EPollManager that is notifying this callback
  /// @param efd the file descriptor that has the event
  /// @param events the bitmask of events detected by epoll
  ///
  void notify(AbstractEPollManager& epoll, int efd, uint32_t events) override;

  ///
  /// Stop the timer.
  ///
  /// @return true if the stop call succeeded.
  ///
  [[nodiscard]] bool stop() override;

  //
  /// Get the next projected time that this timer will fire.
  ///
  /// @return the next time this timer expects to fire.
  ///
  [[nodiscard]] jewels::time::SyncTime next_event_time() const;

  ///
  /// @return true if the timer has been started.
  ///
  [[nodiscard]] bool started() const;

private:
  ///
  /// Notify the observer of the timer event
  ///
  void publish(jewels::time::SyncTime current_time);

  /// The next trigger time
  std::optional<jewels::time::SyncTime> next_trigger_;
  /// The re-arm period
  std::optional<std::chrono::nanoseconds> period_;
  /// The observer to notify.
  pinion::Observer* observer_ = nullptr;
};

} // namespace clockwork
