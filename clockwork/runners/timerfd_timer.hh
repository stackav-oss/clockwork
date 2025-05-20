// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/abstract_timer.hh"
#include "clockwork/pinion/observer.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/time/sync_time.hh"

#include <chrono>
#include <cstdint>
#include <mutex>

namespace clockwork
{

///
/// Timer class that provides an interface for triggering a single time event (i.e. not periodic). It will be used to
/// implement the time_since_last_execution event trigger.
///
class TimerfdTimer : public AbstractTimer
{
public:
  ///
  /// Constructor.
  ///
  explicit TimerfdTimer();

  ///
  /// Destructor.
  ///
  ~TimerfdTimer() override;

  TimerfdTimer(const TimerfdTimer&) = delete;
  TimerfdTimer& operator=(const TimerfdTimer&) = delete;
  TimerfdTimer(TimerfdTimer&&) = delete;
  TimerfdTimer& operator=(TimerfdTimer&&) = delete;

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

private:
  ///
  /// Arms the timer with the given duration
  ///
  /// @return true if the set succeeded; note that failure is unexpected except
  /// in case of a bug. errno will have the reason in this case.
  [[nodiscard]] bool set_timer(jewels::time::SyncTime trigger_at, std::chrono::nanoseconds period);

  ///
  /// mutex to synchronize between the reset thread (cog execution) and trigger thread (epoll)
  ///
  mutable std::mutex mutex_;

  ///
  /// The timer file descriptor.
  ///
  jewels::filesystem::FileDescriptor descriptor_;

  ///
  /// The observer to notify.
  ///
  pinion::Observer* observer_ = nullptr;

  ///
  /// The target time
  /// If the timer wakes before this time it rearms with the remaining duration
  ///
  jewels::time::SyncTime when_{};

  ///
  /// The period
  ///
  std::chrono::nanoseconds period_{};
};

} // namespace clockwork
