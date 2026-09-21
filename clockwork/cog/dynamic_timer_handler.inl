// IWYU pragma: private, include "clockwork/cog/dynamic_timer_handler.hh"
#pragma once
#include "clockwork/cog/dynamic_timer_handler.hh"

#include "clockwork/common/abstract_timer.hh"
#include "clockwork/dial/cond_dynamic_timer.hh"
#include "clockwork/pinion/observer.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/time/sync_time.hh"

#include <chrono>
#include <compare>
#include <memory>
#include <mutex>
#include <utility>

namespace clockwork
{

template <typename Policy>
DynamicTimerHandler<Policy>::DynamicTimerHandler(
  std::shared_ptr<AbstractTimer> timer, std::shared_ptr<pinion::Observer> cog_notify)
  : timer_(std::move(timer)), cog_notify_(std::move(cog_notify))
{
}

template <typename Policy>
void DynamicTimerHandler<Policy>::set_triggered(const std::scoped_lock<std::mutex>& /*lock*/)
{
  triggered_ = true;
  armed_ = false;
}

template <typename Policy>
void DynamicTimerHandler<Policy>::notify_observer(const Event& event)
{
  if (cog_notify_)
  {
    cog_notify_->notify(event);
  }
}

template <typename Policy>
void DynamicTimerHandler<Policy>::notify(const Event& event)
{
  {
    const std::scoped_lock lock{mutex_};
    if (!armed_ || event.current_time < trigger_at_)
    {
      return;
    }
    set_triggered(lock);
  }
  notify_observer(event);
}

template <typename Policy>
auto DynamicTimerHandler<Policy>::make_condition(jewels::time::SyncTime /*now*/) const -> ConditionType
{
  const std::scoped_lock lock{mutex_};
  return DynamicTimerCondition(triggered_);
}

template <typename Policy>
bool DynamicTimerHandler<Policy>::update_last_exec_time(jewels::time::SyncTime /*last_exec_time*/, bool was_active)
{
  if (was_active)
  {
    const std::scoped_lock lock{mutex_};
    triggered_ = false;
  }
  return true;
}

template <typename Policy>
jewels::BinaryOutcome DynamicTimerHandler<Policy>::arm(jewels::time::SyncTime trigger_at)
{
  const std::scoped_lock lock{mutex_};
  if (!timer_->start(trigger_at, std::chrono::nanoseconds{0}))
  {
    return jewels::failure;
  }
  armed_ = true;
  triggered_ = false;
  trigger_at_ = trigger_at;
  return jewels::success;
}

template <typename Policy>
jewels::BinaryOutcome DynamicTimerHandler<Policy>::disarm()
{
  const std::scoped_lock lock{mutex_};
  if (!armed_)
  {
    triggered_ = false;
    return jewels::success;
  }
  if (!timer_->stop())
  {
    return jewels::failure;
  }
  armed_ = false;
  triggered_ = false;
  return jewels::success;
}

template <typename Policy>
bool DynamicTimerHandler<Policy>::is_armed() const
{
  const std::scoped_lock lock{mutex_};
  return armed_;
}

template <typename Policy>
bool DynamicTimerHandler<Policy>::has_fired() const
{
  const std::scoped_lock lock{mutex_};
  return triggered_;
}

template <typename Policy>
void DynamicTimerHandler<Policy>::notify_if_triggered(jewels::time::SyncTime now)
{
  {
    const std::scoped_lock lock{mutex_};
    if (!(armed_ && now >= trigger_at_))
    {
      return;
    }
    set_triggered(lock);
  }
  notify_observer(Event{.current_time = now});
}

} // namespace clockwork
