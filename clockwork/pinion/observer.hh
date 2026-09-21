// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/time/sync_time.hh"

namespace clockwork::pinion
{

/// A type erased observer class used to receive notifications of new
/// messages on a channel.
class Observer
{
public:
  /// Struct for passing parameters into the `notify()` call
  struct Event
  {
    // Time of the notification
    jewels::time::SyncTime current_time;
  };

  Observer() = default;

  Observer(const Observer&) = default;
  Observer(Observer&&) = default;
  Observer& operator=(const Observer&) = default;
  Observer& operator=(Observer&&) = default;

  virtual ~Observer();

  /// Notify the observer of a new message on a channel.
  /// @param event struct containing details of the event that triggered the notification
  virtual void notify(const Event& event) = 0;
};

} // namespace clockwork::pinion
