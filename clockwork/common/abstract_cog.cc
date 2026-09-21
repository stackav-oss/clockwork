// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/abstract_cog.hh"

#include "clockwork/common/abstract_cog_queue.hh"
#include "clockwork/common/abstract_timer.hh"
#include "clockwork/common/cog_envelope.hh"
#include "clockwork/pinion/observer.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/memory/pointers.hh"

#include <chrono>
#include <memory>
#include <mutex>
#include <utility>

namespace clockwork
{
namespace
{
class PublisherThrottleTimerObserver final : public pinion::Observer
{
public:
  explicit PublisherThrottleTimerObserver(const jewels::memory::ObjectPtr<AbstractCogQueue> queue)
    : queue_(queue)
  {
  }

  void notify(const Event& /*event*/) override
  {
    queue_->notify();
  }

private:
  jewels::memory::ObjectPtr<AbstractCogQueue> queue_;
};
} // namespace

AbstractCog::AbstractCog(jewels::memory::ObjectPtr<AbstractCogQueue> queue)
  : queue_(queue)
{
}

AbstractCog::~AbstractCog() = default;

bool AbstractCog::has_rate_limited_publishers() const
{
  return false;
}

jewels::BinaryOutcome AbstractCog::set_publisher_throttle_timer(std::shared_ptr<AbstractTimer> timer)
{
  const std::scoped_lock lock{publisher_throttle_timer_mutex_};
  if (!timer || publisher_throttle_timer_)
  {
    return jewels::failure;
  }
  publisher_throttle_timer_observer_ = std::make_shared<PublisherThrottleTimerObserver>(queue_);
  timer->set_observer(publisher_throttle_timer_observer_.get());
  publisher_throttle_timer_ = std::move(timer);
  return jewels::success;
}

jewels::BinaryOutcome AbstractCog::arm_publisher_throttle_timer(const jewels::time::SyncTime throttled_until)
{
  const std::scoped_lock lock{publisher_throttle_timer_mutex_};
  if (!publisher_throttle_timer_)
  {
    return jewels::failure;
  }
  if (publisher_throttle_timer_deadline_ == throttled_until)
  {
    return jewels::success;
  }
  if (!publisher_throttle_timer_->start(throttled_until, std::chrono::nanoseconds::zero()))
  {
    return jewels::failure;
  }
  publisher_throttle_timer_deadline_ = throttled_until;
  return jewels::success;
}

void AbstractCog::notify_ready_queue()
{
  queue_->notify();
}

void AbstractCog::add_to_ready_queue(jewels::time::SyncTime ready_time)
{
  queue_->push(CogEnvelope{.ready_time = ready_time, .cog = jewels::memory::make_non_null_from_ref(*this)});
}

} // namespace clockwork
