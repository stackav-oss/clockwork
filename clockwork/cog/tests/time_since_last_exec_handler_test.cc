// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/time_since_last_exec_handler.hh"
#include "clockwork/common/abstract_timer.hh"
#include "clockwork/common/process_description.hh"
#include "clockwork/pinion/observer.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>
#include <gsl/util>

#include <chrono>
#include <compare>
#include <cstdint>
#include <memory>
#include <optional>
#include <tuple>

using namespace std::chrono_literals;

namespace clockwork
{
namespace
{

struct TimeSinceLastExecPolicy
{
  static constexpr auto threshold_ns = 10'000'000; // 10ms
  static constexpr auto endpoint_id =
    jewels::Uuid<common::EndpointClassId>::from_string("bfd135f1-1c7b-4d1a-986e-1bfe1ee6c303").value();
};

using HandlerType = TimeSinceLastExecHandler<TimeSinceLastExecPolicy>;
using Status = typename HandlerType::Status;

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
    when_ = trigger_at;
    return true;
  }

  [[nodiscard]] bool stop() override
  {
    when_ = {};
    return true;
  }

  void notify(AbstractEPollManager& /*epoll*/, int /*efd*/, uint32_t /*events*/) override {}

  void update(jewels::time::SyncTime now)
  {
    if (when_ && now >= *when_)
    {
      when_ = {};
      if (observer_ != nullptr)
      {
        observer_->notify({});
      }
    }
  }

  pinion::Observer* observer_{nullptr};
  std::optional<jewels::time::SyncTime> when_;
};

TEST_CASE("make_condition", "[TimeSinceExecHandler, threshold_ns=10ms]")
{
  auto timer = std::make_shared<TestTimer>();
  auto handler = HandlerType(timer, nullptr);
  std::ignore = handler.update_last_exec_time(jewels::time::SyncTime{}, false);
  timer->observer_ = &handler;

  SECTION("now=0ms, last_exec=0ms")
  {
    auto now = jewels::time::SyncTime{};
    timer->update(now);
    auto cond = handler.make_condition(now);
    REQUIRE_FALSE(cond);
    REQUIRE(std::chrono::milliseconds{0} == cond.get_time_since_last_exec());
  }

  SECTION("now=1ms, last_exec=0ms")
  {
    auto now = jewels::time::SyncTime{std::chrono::milliseconds{1}};
    timer->update(now);
    auto cond = handler.make_condition(now);
    REQUIRE_FALSE(cond);
    REQUIRE(std::chrono::milliseconds{1} == cond.get_time_since_last_exec());
  }

  SECTION("now=10ms, last_exec=0ms")
  {
    auto now = jewels::time::SyncTime{std::chrono::milliseconds{10}};
    timer->update(now);
    auto cond = handler.make_condition(now);
    REQUIRE(cond);
    REQUIRE(std::chrono::milliseconds{10} == cond.get_time_since_last_exec());
  }

  SECTION("now=11ms, last_exec=0ms")
  {
    auto now = jewels::time::SyncTime{std::chrono::milliseconds{11}};
    timer->update(now);
    auto cond = handler.make_condition(now);
    REQUIRE(cond);
    REQUIRE(std::chrono::milliseconds{11} == cond.get_time_since_last_exec());
  }
}

TEST_CASE("update_last_exec_time (inactive)", "[TimeSinceExecHandler, threshold_ns=10ms]")
{
  auto timer = std::make_shared<TestTimer>();
  auto handler = HandlerType(timer, nullptr);

  SECTION("last_exec=0ms")
  {
    auto last_exec_time = jewels::time::SyncTime{std::chrono::milliseconds{0}};
    REQUIRE(
      handler.update_last_exec_time(last_exec_time, false) ==
      Status{.last_exec_time = last_exec_time, .expected_next_trigger = last_exec_time + 10ms});
  }

  SECTION("last_exec=10ms")
  {
    auto last_exec_time = jewels::time::SyncTime{std::chrono::milliseconds{10}};
    REQUIRE(
      handler.update_last_exec_time(last_exec_time, false) ==
      Status{.last_exec_time = last_exec_time, .expected_next_trigger = last_exec_time + 10ms});
  }
}

TEST_CASE("update_last_exec_time (active)", "[TimeSinceExecHandler, threshold_ns=10ms]")
{
  auto timer = std::make_shared<TestTimer>();
  auto handler = HandlerType(timer, nullptr);

  SECTION("last_exec=0ms")
  {
    auto last_exec_time = jewels::time::SyncTime{std::chrono::milliseconds{0}};
    REQUIRE(
      handler.update_last_exec_time(last_exec_time, true) ==
      Status{.last_exec_time = last_exec_time, .expected_next_trigger = {}});
  };

  SECTION("last_exec=10ms")
  {
    auto last_exec_time = jewels::time::SyncTime{std::chrono::milliseconds{10}};
    REQUIRE(
      handler.update_last_exec_time(last_exec_time, true) ==
      Status{.last_exec_time = last_exec_time, .expected_next_trigger = {}});
  }
}

} // namespace
} // namespace clockwork
