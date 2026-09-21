// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/cog_timers.hh"
#include "clockwork/cog/dynamic_timer_handler.hh"
#include "clockwork/common/abstract_timer.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/dial/cond_dynamic_timer.hh"
#include "clockwork/dial/cond_time_since_last_exec.hh"
#include "clockwork/pinion/observer.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/container/compare.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_shared_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>
#include <gsl/util>

#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <optional>
#include <tuple>

namespace clockwork
{
namespace
{

class TestTimer : public AbstractTimer
{
public:
  [[nodiscard]] int32_t descriptor() const override
  {
    return 0;
  }

  void set_observer(pinion::Observer* /*observer*/) override {}
  [[nodiscard]] bool start(jewels::time::SyncTime trigger_at, std::chrono::nanoseconds /* period */) override
  {
    when = trigger_at;
    ++start_count;
    return true;
  }

  [[nodiscard]] bool stop() override
  {
    when = {};
    ++stop_count;
    return true;
  }

  void notify(AbstractEPollManager& /*epoll*/, int /*efd*/, uint32_t /*events*/) override {}

  void update(jewels::time::SyncTime now)
  {
    if (when && now >= *when)
    {
      when = {};
      if (observer != nullptr)
      {
        observer->notify({.current_time = now});
      }
    }
  }

  size_t start_count = {};
  size_t stop_count = {};
  std::optional<jewels::time::SyncTime> when;
  pinion::Observer* observer{nullptr};
};

class TestCog
{
public:
  void notify(jewels::time::SyncTime /*current_time*/) {}
};

template <typename... Policies>
struct CogTimersFixture // NOLINT(clang-analyzer-optin.performance.Padding) Test code so performance is not a concern.
{
  static constexpr auto policy_count = sizeof...(Policies);

  CogTimersFixture()
    : timer(jewels::memory::MemoryResource{std::pmr::new_delete_resource()})
  {
  }

  TestCog cog;
  CogTimers<Policies...> timer;
};

struct TimerPolicy
{
  static constexpr auto threshold_ns = 10'000'000; // 10ms
  static constexpr auto endpoint_id =
    jewels::Uuid<common::EndpointClassId>::from_string("b6e2b628-62ba-4c73-b07e-b2ce77a742b4").value();
};

using TimerPolicyFixture = CogTimersFixture<TimerPolicy>;

TEST_CASE_METHOD(TimerPolicyFixture, "execution", "[cog_timer]")
{
  auto underlying_timer = std::make_shared<TestTimer>();
  auto observer =
    timer.set_handle(TimerPolicy::endpoint_id, underlying_timer, jewels::memory::make_non_null_from_ref(cog));
  REQUIRE(observer);
  REQUIRE(timer.validate());
  underlying_timer->observer = observer->get();

  SECTION("set unknown timer")
  {
    static constexpr auto unknown_id =
      jewels::Uuid<common::EndpointClassId>::from_string("cc89cc22-8438-4248-8b40-018a81379ce7").value();
    REQUIRE_FALSE(timer.set_handle(unknown_id, underlying_timer, jewels::memory::make_non_null_from_ref(cog)));
  }

  SECTION("make_conditions now=0ms, last_exec=0ms")
  {
    auto last_exec = jewels::time::SyncTime{std::chrono::milliseconds{0}};
    auto now = jewels::time::SyncTime{std::chrono::milliseconds{0}};
    auto conditions = timer.make_conditions(now);
    REQUIRE(timer.update_last_exec_time(last_exec, conditions));
    underlying_timer->update(now);
    conditions = timer.make_conditions(now);
    const auto& cond = std::get<0>(conditions);
    REQUIRE_FALSE(cond);
  }

  SECTION("make_conditions now=10ms, last_exec=0ms")
  {
    auto last_exec = jewels::time::SyncTime{std::chrono::milliseconds{0}};
    auto now = jewels::time::SyncTime{std::chrono::milliseconds{10}};
    auto conditions = timer.make_conditions(now);
    REQUIRE(timer.update_last_exec_time(last_exec, conditions));
    underlying_timer->update(now);
    conditions = timer.make_conditions(now);
    const auto& cond = std::get<0>(conditions);
    REQUIRE(cond);
    REQUIRE(std::chrono::milliseconds{10} == cond.get_time_since_last_exec());
  }

  SECTION("update_last_exec_time now=10ms, last_exec=0ms")
  {
    auto now = jewels::time::SyncTime{std::chrono::milliseconds{10}};
    auto last_exec_time = jewels::time::SyncTime{std::chrono::milliseconds{10}};
    auto conditions = timer.make_conditions(now);
    REQUIRE(timer.update_last_exec_time(last_exec_time, conditions));
    REQUIRE(1 == underlying_timer->start_count);

    underlying_timer->update(now);
    {
      conditions = timer.make_conditions(now);
      const auto& cond = std::get<0>(conditions);
      REQUIRE_FALSE(cond);
    }

    now += std::chrono::milliseconds{10};

    underlying_timer->update(now);
    {
      conditions = timer.make_conditions(now);
      const auto& cond = std::get<0>(conditions);
      REQUIRE(cond);
    }
  }
}

using ZeroTimersPolicyFixture = CogTimersFixture<>;

TEST_CASE_METHOD(ZeroTimersPolicyFixture, "zero timers", "[cog_timer]")
{
  REQUIRE(timer.validate());

  auto now = jewels::time::SyncTime{};
  auto last_exec_time = jewels::time::SyncTime{};
  auto conditions = timer.make_conditions(now);
  REQUIRE(0 == std::tuple_size<decltype(conditions)>());
  REQUIRE(timer.update_last_exec_time(last_exec_time, conditions));
}

struct DynamicTimerPolicy
{
  using HandlerType = DynamicTimerHandler<DynamicTimerPolicy>;
  static constexpr auto endpoint_id =
    jewels::Uuid<common::EndpointClassId>::from_string("a1b2c3d4-e5f6-7890-abcd-ef1234567890").value();
};

using DynamicTimerPolicyFixture = CogTimersFixture<DynamicTimerPolicy>;

TEST_CASE_METHOD(DynamicTimerPolicyFixture, "dynamic timer", "[cog_timer]")
{
  auto underlying_timer = std::make_shared<TestTimer>();
  auto observer =
    timer.set_handle(DynamicTimerPolicy::endpoint_id, underlying_timer, jewels::memory::make_non_null_from_ref(cog));
  REQUIRE(observer);
  REQUIRE(timer.validate());
  underlying_timer->observer = observer->get();

  SECTION("initial condition is inactive")
  {
    auto conditions = timer.make_conditions(jewels::time::SyncTime{});
    const auto& cond = std::get<0>(conditions);
    REQUIRE_FALSE(cond.is_active());
  }

  SECTION("condition active after fire")
  {
    auto& handler = timer.get_handler<0>();
    REQUIRE(jewels::ok(handler.arm(jewels::time::SyncTime{std::chrono::milliseconds{50}})));

    underlying_timer->update(jewels::time::SyncTime{std::chrono::milliseconds{50}});

    auto conditions = timer.make_conditions(jewels::time::SyncTime{std::chrono::milliseconds{50}});
    const auto& cond = std::get<0>(conditions);
    REQUIRE(cond.is_active());
  }

  SECTION("update_last_exec_time clears fired state")
  {
    auto& handler = timer.get_handler<0>();
    REQUIRE(jewels::ok(handler.arm(jewels::time::SyncTime{std::chrono::milliseconds{50}})));
    underlying_timer->update(jewels::time::SyncTime{std::chrono::milliseconds{50}});

    auto conditions = timer.make_conditions(jewels::time::SyncTime{std::chrono::milliseconds{50}});
    REQUIRE(std::get<0>(conditions).is_active());

    REQUIRE(timer.update_last_exec_time(jewels::time::SyncTime{std::chrono::milliseconds{50}}, conditions));

    conditions = timer.make_conditions(jewels::time::SyncTime{std::chrono::milliseconds{50}});
    REQUIRE_FALSE(std::get<0>(conditions).is_active());
  }
}

using MixedTimerFixture = CogTimersFixture<TimerPolicy, DynamicTimerPolicy>;

TEST_CASE_METHOD(MixedTimerFixture, "mixed periodic and dynamic timers", "[cog_timer]")
{
  auto periodic_timer = std::make_shared<TestTimer>();
  auto dynamic_timer = std::make_shared<TestTimer>();

  auto obs1 = timer.set_handle(TimerPolicy::endpoint_id, periodic_timer, jewels::memory::make_non_null_from_ref(cog));
  REQUIRE(obs1);
  periodic_timer->observer = obs1->get();

  auto obs2 =
    timer.set_handle(DynamicTimerPolicy::endpoint_id, dynamic_timer, jewels::memory::make_non_null_from_ref(cog));
  REQUIRE(obs2);
  dynamic_timer->observer = obs2->get();

  REQUIRE(timer.validate());

  SECTION("both timer types coexist in conditions tuple")
  {
    auto now = jewels::time::SyncTime{std::chrono::milliseconds{0}};
    auto conditions = timer.make_conditions(now);
    REQUIRE(2 == std::tuple_size<decltype(conditions)>());

    // Periodic timer not yet fired
    REQUIRE_FALSE(std::get<0>(conditions));
    // Dynamic timer not fired
    REQUIRE_FALSE(std::get<1>(conditions).is_active());
  }

  SECTION("periodic fires while dynamic stays idle")
  {
    auto now = jewels::time::SyncTime{std::chrono::milliseconds{10}};
    auto conditions = timer.make_conditions(now);
    REQUIRE(timer.update_last_exec_time(jewels::time::SyncTime{}, conditions));
    periodic_timer->update(now);
    conditions = timer.make_conditions(now);

    REQUIRE(std::get<0>(conditions));                   // periodic fired
    REQUIRE_FALSE(std::get<1>(conditions).is_active()); // dynamic still idle
  }

  SECTION("dynamic fires while periodic stays idle")
  {
    auto& handler = timer.get_handler<1>();
    REQUIRE(jewels::ok(handler.arm(jewels::time::SyncTime{std::chrono::milliseconds{5}})));
    dynamic_timer->update(jewels::time::SyncTime{std::chrono::milliseconds{5}});

    auto conditions = timer.make_conditions(jewels::time::SyncTime{std::chrono::milliseconds{5}});
    REQUIRE_FALSE(std::get<0>(conditions));       // periodic not fired yet (5ms < 10ms threshold)
    REQUIRE(std::get<1>(conditions).is_active()); // dynamic fired
  }
}

} // namespace
} // namespace clockwork
