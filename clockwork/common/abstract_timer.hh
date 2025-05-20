// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/abstract_epoll_manager.hh"
#include "clockwork/pinion/observer.hh"
#include "jewels/time/sync_time.hh"

#include <chrono>
#include <cstdint>

namespace clockwork
{

///
/// AbstractTimer interface for use with clockwork runners.
///
/// The AbstractTimer should be registered with the EpollHandler using the provided
/// descriptor. An internal observer can be registered using `set_observer` that will
/// be notified any time the timer triggers.
///
class AbstractTimer : public AbstractEPollCallback
{
public:
  ///
  /// Constructor.
  ///
  explicit AbstractTimer();

  ///
  /// Destructor.
  ///
  ~AbstractTimer() override;

  AbstractTimer(const AbstractTimer&) = delete;
  AbstractTimer& operator=(const AbstractTimer&) = delete;
  AbstractTimer(AbstractTimer&&) = delete;
  AbstractTimer& operator=(AbstractTimer&&) = delete;

  ///
  /// Get the file descriptor.
  ///
  /// @return The file descriptor associated with this timer.
  ///
  [[nodiscard]] virtual int32_t descriptor() const = 0;

  ///
  /// Set the observer to notify on events.
  ///
  virtual void set_observer(pinion::Observer* observer) = 0;

  ///
  /// Start the timer. This will cancel any active timer.
  ///
  /// @param[in] trigger_at The next desired trigger time
  /// @param[in] period The period for auto re-triggering (or 0 for single-shot)
  /// @return true if start succeeded.
  ///
  [[nodiscard]] virtual bool start(jewels::time::SyncTime trigger_at, std::chrono::nanoseconds period) = 0;

  ///
  /// Stop the timer.
  ///
  /// @return true if the stop call succeeded.
  ///
  [[nodiscard]] virtual bool stop() = 0;
};

} // namespace clockwork
