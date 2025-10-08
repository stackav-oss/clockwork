// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/runners/timerfd_timer.hh"

#include "jewels/filesystem/error_code.hh"
#include "jewels/time/sync_time.hh"

#include <cerrno>
#include <chrono>
#include <ctime>
#include <exception>
#include <stdexcept>
#include <sys/timerfd.h>
#include <unistd.h>

namespace clockwork
{

TimerfdTimer::TimerfdTimer()
  : descriptor_(timerfd_create(CLOCK_REALTIME, TFD_NONBLOCK))
{
  if (!descriptor_)
  {
    throw std::runtime_error(jewels::filesystem::ErrorCode(errno).message());
  }
}

TimerfdTimer::~TimerfdTimer() = default;

int32_t TimerfdTimer::descriptor() const
{
  return *descriptor_;
}

void TimerfdTimer::set_observer(pinion::Observer* observer)
{
  observer_ = observer;
}

bool TimerfdTimer::start(jewels::time::SyncTime trigger_at, std::chrono::nanoseconds period)
{
  const std::scoped_lock lock(mutex_);
  when_ = trigger_at;
  period_ = period;
  return set_timer(trigger_at, period_);
}

bool TimerfdTimer::set_timer(jewels::time::SyncTime trigger_at, std::chrono::nanoseconds period)
{
  const auto epoch_time = trigger_at.time_since_epoch();
  const auto sec = std::chrono::duration_cast<std::chrono::seconds>(epoch_time);
  const auto nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(epoch_time - sec);
  const auto period_sec = std::chrono::duration_cast<std::chrono::seconds>(period);
  const auto period_nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(period - period_sec);
  const auto flags = TFD_TIMER_ABSTIME;
  const auto spec = itimerspec{
    .it_interval = {.tv_sec = period_sec.count(), .tv_nsec = period_nsec.count()},
    .it_value = {.tv_sec = sec.count(), .tv_nsec = nsec.count()},
  };
  return (timerfd_settime(*descriptor_, flags, &spec, nullptr) == 0);
}

void TimerfdTimer::notify(AbstractEPollManager& /*epoll*/, int /*efd*/, uint32_t /*events*/)
{
  uint64_t event_count = 0;
  while (read(*descriptor_, &event_count, sizeof(event_count)) > 0)
  {
    {
      const std::scoped_lock lock(mutex_);
      auto earliness = when_ - jewels::time::SyncClock::now();
      if (earliness.count() > 0)
      {
        if (!set_timer(when_, period_))
        {
          // Timers are documented that error return is unexpected; this indicates a kernel syscall failure
          std::terminate();
        }
        return;
      }
    }
    for (; event_count != 0; --event_count)
    {
      if (observer_ != nullptr)
      {
        observer_->notify({.current_time = jewels::time::SyncClock::now()});
      }
    }
  }
}

bool TimerfdTimer::stop()
{
  auto flags = 0;
  auto spec = itimerspec{};
  return (timerfd_settime(*descriptor_, flags, &spec, nullptr) == 0);
}

} // namespace clockwork
