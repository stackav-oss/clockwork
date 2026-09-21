// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/observer.hh"
#include "clockwork/runners/deterministic_timer.hh"
#include "jewels/time/sync_time.hh"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>

namespace clockwork
{
class MockObserver : public pinion::Observer
{
public:
  /// Notify the observer of a new message on a channel.
  /// @param event struct containing details of the event that triggered the notification
  void notify(const Event& /*event*/) override
  {
    ++num_notify_calls;
  }
  int num_notify_calls = 0;
};

TEST_CASE("Test deterministic timer (non-repeating)")
{
  using namespace std::chrono_literals;
  constexpr auto start_time = jewels::time::SyncTime(3000ms);

  auto timer = DeterministicTimer();
  auto observer = std::make_unique<MockObserver>();
  timer.set_observer(observer.get());

  CHECK(!timer.started());

  auto current_time = start_time;
  constexpr auto timer_duration = 100ms;
  constexpr auto half_of_timer_duration = 50ms;
  CHECK(timer.start(current_time + timer_duration, 0ns));
  timer.update(current_time);
  CHECK(timer.next_event_time() == start_time + timer_duration);
  current_time = start_time + timer_duration;
  timer.update(current_time);
  timer.update(current_time);
  CHECK(observer->num_notify_calls == 1);

  CHECK(timer.start(current_time + timer_duration, 0ns));
  timer.update(current_time);
  CHECK(observer->num_notify_calls == 1);

  current_time = current_time + half_of_timer_duration;
  timer.update(current_time);
  CHECK(observer->num_notify_calls == 1);

  current_time = current_time + half_of_timer_duration;
  timer.update(current_time);
  CHECK(observer->num_notify_calls == 2);

  // Does not repeat
  current_time = current_time + timer_duration;
  timer.update(current_time);
  CHECK(observer->num_notify_calls == 2);
}

TEST_CASE("Test deterministic timer (repeating)")
{
  using namespace std::chrono_literals;
  constexpr auto start_time = jewels::time::SyncTime(3000ms);

  auto timer = DeterministicTimer();
  auto observer = std::make_unique<MockObserver>();
  timer.set_observer(observer.get());

  CHECK(!timer.started());

  auto current_time = start_time;
  constexpr auto period = 100ms;
  CHECK(timer.start(current_time + period, period));
  timer.update(current_time);
  CHECK(timer.next_event_time() == start_time + period);
  CHECK(observer->num_notify_calls == 0);
  current_time = start_time + period;
  timer.update(current_time);
  CHECK(observer->num_notify_calls == 1);
  timer.update(current_time);
  CHECK(observer->num_notify_calls == 1);

  current_time = current_time + period;
  timer.update(current_time);
  CHECK(observer->num_notify_calls == 2);

  current_time = current_time + period;
  timer.update(current_time);
  CHECK(observer->num_notify_calls == 3);
}
} // namespace clockwork
