// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/tests/support/fake_epoll_mananger.hh"
#include "clockwork/pinion/observer.hh"
#include "clockwork/runners/timerfd_timer.hh"
#include "jewels/time/sync_time.hh"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <thread>

using namespace std::chrono_literals;

namespace clockwork
{
namespace
{

class TestObserver : public pinion::Observer
{
public:
  TestObserver() = default;
  ~TestObserver() override = default;

  TestObserver(const TestObserver&) = delete;
  TestObserver& operator=(const TestObserver&) = delete;
  TestObserver(TestObserver&&) = delete;
  TestObserver& operator=(TestObserver&&) = delete;

  void notify(const Event& /*event*/) override
  {
    ++notify_count_;
  }

  [[nodiscard]] uint64_t notify_count() const
  {
    return notify_count_;
  }

private:
  uint64_t notify_count_ = 0;
};

TEST_CASE("execution", "[TimerfdTimer OneShot]")
{
  auto epoll = testing::FakeEPollManager();
  auto observer = TestObserver();
  auto timer = TimerfdTimer();
  timer.set_observer(&observer);

  REQUIRE(0 == observer.notify_count());

  SECTION("never started")
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    timer.notify(epoll, {}, {});

    REQUIRE(0 == observer.notify_count());
  }

  SECTION("start once")
  {
    REQUIRE(timer.start(jewels::time::SyncClock::now() + std::chrono::milliseconds(1), 0ns));

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    timer.notify(epoll, {}, {});

    REQUIRE(1 == observer.notify_count());

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    timer.notify(epoll, {}, {});

    REQUIRE(1 == observer.notify_count());
  }

  SECTION("start resets the timer")
  {
    const auto now = jewels::time::SyncClock::now();
    REQUIRE(timer.start(now + std::chrono::milliseconds(500), 0ns));
    REQUIRE(timer.start(now + std::chrono::milliseconds(1), 0ns));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    timer.notify(epoll, {}, {});

    REQUIRE(1 == observer.notify_count());

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    timer.notify(epoll, {}, {});

    REQUIRE(1 == observer.notify_count());
  }

  SECTION("Setting to a time in the past triggers immediately")
  {
    const auto now = jewels::time::SyncClock::now();
    REQUIRE(timer.start(now - std::chrono::milliseconds(1), 0ns));

    // Let the kernel tick once.
    std::this_thread::sleep_for(1ms);

    timer.notify(epoll, {}, {});

    REQUIRE(1 == observer.notify_count());
  }

  SECTION("stop cancels current timer")
  {
    REQUIRE(timer.start(jewels::time::SyncClock::now() + std::chrono::milliseconds(500), 0ns));
    REQUIRE(timer.stop());

    std::this_thread::sleep_for(std::chrono::seconds(1));

    timer.notify(epoll, {}, {});

    REQUIRE(0 == observer.notify_count());
  }
}

TEST_CASE("execution", "[TimerfdTimer Repeating]")
{
  auto epoll = testing::FakeEPollManager();
  auto observer = TestObserver();
  auto timer = TimerfdTimer();
  timer.set_observer(&observer);

  REQUIRE(0 == observer.notify_count());

  SECTION("never started")
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    timer.notify(epoll, {}, {});

    REQUIRE(0 == observer.notify_count());
  }

  SECTION("start")
  {
    // Should be immediately triggered
    REQUIRE(timer.start(jewels::time::SyncClock::now(), 10ms));

    // Let the kernel tick once.
    std::this_thread::sleep_for(1ms);

    timer.notify(epoll, {}, {});

    const auto original = observer.notify_count();
    REQUIRE(1 <= original);

    std::this_thread::sleep_for(10ms);

    timer.notify(epoll, {}, {});

    // Possible we get more if more time has elapsed.
    REQUIRE(original + 1 <= observer.notify_count());
  }

  SECTION("start resets the timer")
  {
    const auto now = jewels::time::SyncClock::now();
    REQUIRE(timer.start(now + 1000ms, 0ms));
    REQUIRE(timer.start(now, 10ms));

    std::this_thread::sleep_for(100ms);

    timer.notify(epoll, {}, {});

    // Possible we get more if more time has elapsed.
    REQUIRE(11 <= observer.notify_count());
  }

  SECTION("start changes the period")
  {
    const auto now = jewels::time::SyncClock::now();
    REQUIRE(timer.start(now + 1000ms, 10ms));
    REQUIRE(timer.start(now, 0ms));

    std::this_thread::sleep_for(100ms);

    timer.notify(epoll, {}, {});

    REQUIRE(1 == observer.notify_count());
  }
}

} // namespace
} // namespace clockwork
