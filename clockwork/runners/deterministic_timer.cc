// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/runners/deterministic_timer.hh"

#include <chrono>
#include <compare>

using namespace std::chrono_literals;

namespace clockwork
{

DeterministicTimer::DeterministicTimer() = default;

void DeterministicTimer::update(jewels::time::SyncTime now)
{
  if (next_trigger_ && (now >= *next_trigger_))
  {
    publish(now);
  }
}

bool DeterministicTimer::start(jewels::time::SyncTime trigger_at, std::chrono::nanoseconds period)
{
  next_trigger_ = trigger_at;
  if (period != 0ns)
  {
    period_ = period;
  }
  else
  {
    period_ = {};
  }
  return true;
}

bool DeterministicTimer::stop()
{
  next_trigger_ = {};
  period_ = {};
  return true;
}

void DeterministicTimer::set_observer(pinion::Observer* observer)
{
  observer_ = observer;
}

int32_t DeterministicTimer::descriptor() const
{
  return -1;
}

void DeterministicTimer::notify(AbstractEPollManager& /*unused*/, int /*unused*/, uint32_t /*unused*/) {}

void DeterministicTimer::publish(jewels::time::SyncTime current_time)
{
  if (observer_ != nullptr)
  {
    observer_->notify({.current_time = current_time});
  }
  if (period_ && next_trigger_)
  {
    *next_trigger_ += *period_;
  }
  else
  {
    next_trigger_ = {};
  }
}

jewels::time::SyncTime DeterministicTimer::next_event_time() const
{
  if (next_trigger_)
  {
    return *next_trigger_;
  }

  return jewels::time::SyncTime::max();
}

bool DeterministicTimer::started() const
{
  return static_cast<bool>(next_trigger_);
}
} // namespace clockwork
