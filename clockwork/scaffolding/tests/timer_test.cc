// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/exec_tools.hh"
#include "clockwork/common/process_description.hh"
#include "clockwork/pinion/observer.hh"
#include "clockwork/pinion/tests/support/pub_sub.hh"
#include "clockwork/runners/epoll_manager.hh"
#include "clockwork/runners/timerfd_timer.hh"
#include "clockwork/scaffolding/abstract_casing.hh"
#include "clockwork/scaffolding/tests/support/mock_casing.hh"
#include "clockwork/scaffolding/timer.hh"
#include "jewels/container/compare.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>
#include <trompeloeil/catch2.hpp> // IWYU pragma: keep (registers catch2 as the error handler)
#include <trompeloeil/mock.hpp>
#include <xxh3.h>

#include <chrono>
#include <functional>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

namespace clockwork::scaffolding
{
namespace
{

TEST_CASE("setup_timers + connect_timers")
{
  using RetT = jewels::expected<std::shared_ptr<pinion::Observer>, AbstractCasing::Error>;

  const auto timer1_id = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  const auto timer2_id = jewels::Uuid<common::EndpointInstanceId>::random_uuid();

  const jewels::memory::MemoryResource memres_sys(std::pmr::new_delete_resource());

  std::vector<common::TimerInstanceDescriptionTap> configs;
  configs.emplace_back();
  configs.back().get_mutable_timer_id() = timer1_id;
  configs.back().get_underlying_instance_path_name().set_truncate("timer1");
  configs.emplace_back();
  configs.back().get_mutable_timer_id() = timer2_id;
  configs.back().get_underlying_instance_path_name().set_truncate("timer2");

  SECTION("Online timers")
  {
    auto timers = setup_timers(configs, memres_sys);
    REQUIRE(timers);
    REQUIRE(timers->size() == configs.size());
    REQUIRE(timers->at(timer1_id) != nullptr);
    REQUIRE(timers->at(timer2_id) != nullptr);

    MockCasing casing;
    auto observer = std::make_shared<testing::TestObserver>();

    SECTION("okay")
    {
      REQUIRE_CALL(casing, try_connect_timer(timer1_id, timers->at(timer1_id))).RETURN(RetT{observer});
      REQUIRE_CALL(casing, try_connect_timer(timer2_id, timers->at(timer2_id))).RETURN(RetT{observer});
      auto result = connect_timers(configs, memres_sys, *timers, casing);
      REQUIRE(result);
      REQUIRE(result->size() == 2);
      REQUIRE(result->at(0) == observer);
    }
    SECTION("missing timer")
    {
      timers->erase(timers->find(timer2_id));
      REQUIRE_CALL(casing, try_connect_timer(timer1_id, timers->at(timer1_id))).RETURN(RetT{observer});
      auto result = connect_timers(configs, memres_sys, *timers, casing);
      REQUIRE(!result);
    }
    SECTION("connect failure")
    {
      REQUIRE_CALL(casing, try_connect_timer(timer1_id, timers->at(timer1_id))).RETURN(RetT{observer});
      REQUIRE_CALL(casing, try_connect_timer(timer2_id, timers->at(timer2_id)))
        .RETURN(jewels::unexpected(AbstractCasing::Error::invalid_instance_uuid));
      auto result = connect_timers(configs, memres_sys, *timers, casing);
      REQUIRE(!result);
    }
  }

  SECTION("Deterministic Timers")
  {
    auto timers =
      setup_deterministic_timers(ExecutionParams{.execution_mode = ExecutionMode::deterministic}, configs, memres_sys);
    REQUIRE(timers);
    REQUIRE(timers->size() == configs.size());
    REQUIRE(timers->at(timer1_id) != nullptr);
    REQUIRE(timers->at(timer2_id) != nullptr);

    MockCasing casing;
    auto observer = std::make_shared<testing::TestObserver>();

    SECTION("okay")
    {
      REQUIRE_CALL(casing, try_connect_timer(timer1_id, timers->at(timer1_id))).RETURN(RetT{observer});
      REQUIRE_CALL(casing, try_connect_timer(timer2_id, timers->at(timer2_id))).RETURN(RetT{observer});
      auto result = connect_timers(configs, memres_sys, *timers, casing);
      REQUIRE(result);
      REQUIRE(result->size() == 2);
      REQUIRE(result->at(0) == observer);
    }
    SECTION("missing timer")
    {
      timers->erase(timers->find(timer2_id));
      REQUIRE_CALL(casing, try_connect_timer(timer1_id, timers->at(timer1_id))).RETURN(RetT{observer});
      auto result = connect_timers(configs, memres_sys, *timers, casing);
      REQUIRE(!result);
    }
    SECTION("connect failure")
    {
      REQUIRE_CALL(casing, try_connect_timer(timer1_id, timers->at(timer1_id))).RETURN(RetT{observer});
      REQUIRE_CALL(casing, try_connect_timer(timer2_id, timers->at(timer2_id)))
        .RETURN(jewels::unexpected(AbstractCasing::Error::invalid_instance_uuid));
      auto result = connect_timers(configs, memres_sys, *timers, casing);
      REQUIRE(!result);
    }
  }
}

TEST_CASE("bind_timers_to_epoll")
{
  const jewels::memory::MemoryResource memres{std::pmr::get_default_resource()};

  const auto timer1_id = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  auto timer1 = std::make_shared<TimerfdTimer>();

  testing::TestObserver observer;
  timer1->set_observer(&observer);

  EPollManager manager{memres};
  bind_timers_to_epoll({{timer1_id, timer1}}, manager);

  auto current_time = jewels::time::SyncClock::now();
  CHECK(manager.wait(std::chrono::milliseconds(50)));
  CHECK_FALSE(observer.event);
  CHECK(timer1->start(current_time + std::chrono::milliseconds(10), 10ms));
  CHECK(manager.wait(std::chrono::milliseconds(-1)));
  CHECK(observer.event);
}

} // namespace
} // namespace clockwork::scaffolding
