// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/cog_timers.hh"
#include "clockwork/common/abstract_timer.hh"
#include "clockwork/common/process_description.hh"
#include "clockwork/dial/cond_time_since_last_exec.hh"
#include "clockwork/pinion/observer.hh"
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
#include <string_view>
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
        observer->notify({});
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
  static constexpr std::string_view name = "TimerPolicy";
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

} // namespace
} // namespace clockwork
