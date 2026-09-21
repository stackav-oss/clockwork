// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/dynamic_timer_handler.hh"
#include "clockwork/common/abstract_timer.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/pinion/observer.hh"
#include "clockwork/runners/deterministic_timer.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>
#include <gsl/util>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <tuple>

namespace clockwork
{
namespace
{

/// Mock observer to track whether notification was forwarded.
class TestNotifyObserver : public pinion::Observer
{
public:
  void notify(const Event& /*event*/) override
  {
    ++notify_count_;
  }

  size_t notify_count_{};
};

struct DynamicTimerPolicy
{
  using HandlerType = DynamicTimerHandler<DynamicTimerPolicy>;
  static constexpr auto endpoint_id =
    jewels::Uuid<common::EndpointClassId>::from_string("d1e2f3a4-b5c6-7890-abcd-ef1234567890").value();
};

using HandlerType = DynamicTimerHandler<DynamicTimerPolicy>;

TEST_CASE("DynamicTimerHandler initial state", "[DynamicTimerHandler]")
{
  auto timer = std::make_shared<DeterministicTimer>();
  auto handler = HandlerType(timer, nullptr);

  SECTION("starts disarmed and not fired")
  {
    REQUIRE_FALSE(handler.is_armed());
    REQUIRE_FALSE(handler.has_fired());
  }

  SECTION("make_condition returns inactive when not fired")
  {
    auto cond = handler.make_condition(jewels::time::SyncTime{});
    REQUIRE_FALSE(cond.is_active());
  }
}

TEST_CASE("DynamicTimerHandler arm and fire", "[DynamicTimerHandler]")
{
  auto timer = std::make_shared<DeterministicTimer>();
  auto cog_notify = std::make_shared<TestNotifyObserver>();
  auto handler = HandlerType(timer, cog_notify);
  timer->set_observer(&handler);

  SECTION("arm sets the timer and reports armed")
  {
    REQUIRE(jewels::ok(handler.arm(jewels::time::SyncTime{std::chrono::milliseconds{100}})));
    REQUIRE(handler.is_armed());
    REQUIRE_FALSE(handler.has_fired());
  }

  SECTION("fire sets triggered and clears armed")
  {
    REQUIRE(jewels::ok(handler.arm(jewels::time::SyncTime{std::chrono::milliseconds{100}})));
    timer->update(jewels::time::SyncTime{std::chrono::milliseconds{100}});

    REQUIRE_FALSE(handler.is_armed());
    REQUIRE(handler.has_fired());
    REQUIRE(1 == cog_notify->notify_count_);

    // make_condition shows active after fire
    auto cond = handler.make_condition(jewels::time::SyncTime{std::chrono::milliseconds{100}});
    REQUIRE(cond.is_active());
  }

  SECTION("notify after disarm is ignored")
  {
    REQUIRE(jewels::ok(handler.arm(jewels::time::SyncTime{std::chrono::milliseconds{100}})));
    REQUIRE(jewels::ok(handler.disarm()));

    handler.notify({.current_time = jewels::time::SyncTime{std::chrono::milliseconds{100}}});

    REQUIRE_FALSE(handler.is_armed());
    REQUIRE_FALSE(handler.has_fired());
    REQUIRE(0 == cog_notify->notify_count_);
  }

  SECTION("notify before re-armed trigger time is ignored")
  {
    REQUIRE(jewels::ok(handler.arm(jewels::time::SyncTime{std::chrono::milliseconds{100}})));
    REQUIRE(jewels::ok(handler.disarm()));
    REQUIRE(jewels::ok(handler.arm(jewels::time::SyncTime{std::chrono::milliseconds{200}})));

    handler.notify({.current_time = jewels::time::SyncTime{std::chrono::milliseconds{100}}});

    REQUIRE(handler.is_armed());
    REQUIRE_FALSE(handler.has_fired());
    REQUIRE(0 == cog_notify->notify_count_);
  }

  SECTION("update_last_exec_time clears triggered when was_active")
  {
    REQUIRE(jewels::ok(handler.arm(jewels::time::SyncTime{std::chrono::milliseconds{100}})));
    timer->update(jewels::time::SyncTime{std::chrono::milliseconds{100}});
    REQUIRE(handler.has_fired());

    auto result = handler.update_last_exec_time(jewels::time::SyncTime{std::chrono::milliseconds{100}}, true);
    REQUIRE(result);
    REQUIRE_FALSE(handler.has_fired());
  }

  SECTION("update_last_exec_time preserves triggered when not was_active")
  {
    REQUIRE(jewels::ok(handler.arm(jewels::time::SyncTime{std::chrono::milliseconds{100}})));
    timer->update(jewels::time::SyncTime{std::chrono::milliseconds{100}});
    REQUIRE(handler.has_fired());

    // Cog executed for a different reason (timer condition was not active),
    // so the triggered flag should be preserved for the next cycle.
    auto result = handler.update_last_exec_time(jewels::time::SyncTime{std::chrono::milliseconds{100}}, false);
    REQUIRE(result);
    REQUIRE(handler.has_fired());
  }

  SECTION("disarm stops the timer")
  {
    REQUIRE(jewels::ok(handler.arm(jewels::time::SyncTime{std::chrono::milliseconds{100}})));
    REQUIRE(handler.is_armed());

    REQUIRE(jewels::ok(handler.disarm()));
    REQUIRE_FALSE(handler.is_armed());

    // Timer should not fire after disarm
    timer->update(jewels::time::SyncTime{std::chrono::milliseconds{100}});
    REQUIRE_FALSE(handler.has_fired());
  }

  SECTION("disarm clears triggered after fire")
  {
    REQUIRE(jewels::ok(handler.arm(jewels::time::SyncTime{std::chrono::milliseconds{100}})));
    timer->update(jewels::time::SyncTime{std::chrono::milliseconds{100}});
    REQUIRE(handler.has_fired());

    REQUIRE(jewels::ok(handler.disarm()));

    REQUIRE_FALSE(handler.is_armed());
    REQUIRE_FALSE(handler.has_fired());
  }

  SECTION("re-arm after fire works correctly")
  {
    REQUIRE(jewels::ok(handler.arm(jewels::time::SyncTime{std::chrono::milliseconds{100}})));
    timer->update(jewels::time::SyncTime{std::chrono::milliseconds{100}});
    REQUIRE(handler.has_fired());

    // Clear the fired state
    std::ignore = handler.update_last_exec_time(jewels::time::SyncTime{std::chrono::milliseconds{100}}, true);

    // Re-arm at a later time
    REQUIRE(jewels::ok(handler.arm(jewels::time::SyncTime{std::chrono::milliseconds{200}})));
    REQUIRE(handler.is_armed());
    REQUIRE_FALSE(handler.has_fired());

    timer->update(jewels::time::SyncTime{std::chrono::milliseconds{200}});
    REQUIRE(handler.has_fired());
    REQUIRE(2 == cog_notify->notify_count_);
  }
}

/// Mock timer that always fails start() and stop().
class FailingTestTimer : public AbstractTimer
{
public:
  [[nodiscard]] int32_t descriptor() const override
  {
    return 0;
  }

  void set_observer(pinion::Observer* /*observer*/) override {}

  [[nodiscard]] bool start(jewels::time::SyncTime /*trigger_at*/, std::chrono::nanoseconds /*period*/) override
  {
    return false;
  }

  [[nodiscard]] bool stop() override
  {
    return false;
  }

  void notify(AbstractEPollManager& /*epoll*/, int /*efd*/, uint32_t /*events*/) override {}
};

/// Mock timer that starts successfully but fails stop().
class StopFailingTestTimer : public AbstractTimer
{
public:
  [[nodiscard]] int32_t descriptor() const override
  {
    return 0;
  }

  void set_observer(pinion::Observer* /*observer*/) override {}

  [[nodiscard]] bool start(jewels::time::SyncTime /*trigger_at*/, std::chrono::nanoseconds /*period*/) override
  {
    return true;
  }

  [[nodiscard]] bool stop() override
  {
    return false;
  }

  void notify(AbstractEPollManager& /*epoll*/, int /*efd*/, uint32_t /*events*/) override {}
};

TEST_CASE("DynamicTimerHandler arm/disarm failure paths", "[DynamicTimerHandler]")
{
  auto failing_timer = std::make_shared<FailingTestTimer>();
  auto handler = HandlerType(failing_timer, nullptr);

  SECTION("arm returns failure when timer start fails")
  {
    REQUIRE(jewels::fails(handler.arm(jewels::time::SyncTime{std::chrono::milliseconds{100}})));
    REQUIRE_FALSE(handler.is_armed());
  }

  SECTION("disarm succeeds when already idle")
  {
    REQUIRE(jewels::ok(handler.disarm()));
  }

  SECTION("disarm returns failure when armed timer stop fails")
  {
    auto stop_failing_timer = std::make_shared<StopFailingTestTimer>();
    auto stop_failing_handler = HandlerType(stop_failing_timer, nullptr);

    REQUIRE(jewels::ok(stop_failing_handler.arm(jewels::time::SyncTime{std::chrono::milliseconds{100}})));
    REQUIRE(jewels::fails(stop_failing_handler.disarm()));
  }
}

} // namespace
} // namespace clockwork
