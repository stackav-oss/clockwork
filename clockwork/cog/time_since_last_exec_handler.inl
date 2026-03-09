// IWYU pragma: private, include "clockwork/cog/time_since_last_exec_handler.hh"
#pragma once
#include "clockwork/cog/time_since_last_exec_handler.hh"

#include "clockwork/common/abstract_timer.hh"
#include "clockwork/dial/cond_time_since_last_exec.hh"
#include "clockwork/pinion/observer.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"

#include <chrono>
#include <compare>
#include <memory>
#include <mutex>
#include <utility>

namespace clockwork
{

template <typename Policy>
TimeSinceLastExecHandler<Policy>::TimeSinceLastExecHandler(
  std::shared_ptr<AbstractTimer> timer, std::shared_ptr<pinion::Observer> cog_notify)
  : timer_(std::move(timer)), cog_notify_(std::move(cog_notify))
{
}

template <typename Policy>
void TimeSinceLastExecHandler<Policy>::notify(const Event& event)
{
  {
    const std::scoped_lock lock{mutex_};
    expected_next_trigger_ += std::chrono::nanoseconds(threshold_ns);
    triggered_ = true;
  }
  if (cog_notify_)
  {
    cog_notify_->notify(event);
  }
}

template <typename Policy>
auto TimeSinceLastExecHandler<Policy>::make_condition(jewels::time::SyncTime now) const
  -> TimeSinceLastExecCondition<threshold_ns>
{
  const std::scoped_lock lock{mutex_};
  const auto time_since_last_exec = (now - last_exec_time_);
  return TimeSinceLastExecCondition<threshold_ns>(triggered_, time_since_last_exec);
}

template <typename Policy>
auto TimeSinceLastExecHandler<Policy>::update_last_exec_time(jewels::time::SyncTime last_exec_time, bool was_active)
  -> jewels::expected<Status, jewels::MonoError>
{
  const std::scoped_lock lock{mutex_};
  last_exec_time_ = last_exec_time;
  triggered_ = false;

  if (was_active)
  {
    return Status{.last_exec_time = last_exec_time_, .expected_next_trigger = expected_next_trigger_};
  }
  const auto trigger_at = std::chrono::nanoseconds(threshold_ns) + last_exec_time_;
  if (!timer_->start(trigger_at, std::chrono::nanoseconds(threshold_ns)))
  {
    return jewels::unexpected{jewels::MonoError{}};
  }
  expected_next_trigger_ = trigger_at;

  return Status{.last_exec_time = last_exec_time_, .expected_next_trigger = expected_next_trigger_};
}

template <typename Policy>
void TimeSinceLastExecHandler<Policy>::notify_if_triggered(jewels::time::SyncTime now)
{
  if (now >= expected_next_trigger_)
  {
    notify(Event{.current_time = now});
  }
}

} // namespace clockwork
