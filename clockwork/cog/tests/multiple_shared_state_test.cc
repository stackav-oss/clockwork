// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/cog_conditions.hh"
#include "clockwork/cog/cog_configs.hh"
#include "clockwork/cog/cog_diagnostics.hh"
#include "clockwork/cog/cog_inputs.hh"
#include "clockwork/cog/cog_memory_resources.hh"
#include "clockwork/cog/cog_publishers.hh"
#include "clockwork/cog/cog_state.hh"
#include "clockwork/cog/cog_states.hh"
#include "clockwork/cog/cog_statistics.hh"
#include "clockwork/cog/cog_timers.hh"
#include "clockwork/cog/detail.hh"
#include "clockwork/cog/simple_cog.hh"
#include "clockwork/common/abstract_cog.hh"
#include "clockwork/common/process_description.hh"
#include "clockwork/runners/online_cog_queue.hh"
#include "clockwork/runners/online_runner.hh"
#include "clockwork/runners/thread_pool.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_shared_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>
#include <gsl/util>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <thread>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace clockwork
{
namespace
{

constexpr auto thread_count = 8;

struct TestState
{
  explicit TestState(jewels::memory::MemoryResource /*resource*/) {}
  std::array<uint32_t, thread_count> counts = {};
};

struct MultiStateCogDial
{
  jewels::time::SyncTime start_time;
  jewels::memory::ObjectPtr<TestState> state1;
  jewels::memory::ObjectPtr<TestState> state2;
};

/// An always ready test cog for testing state locking.
template <size_t index>
struct MultiStateCogPolicy
{
  static constexpr auto cog_id =
    jewels::Uuid<common::EndpointClassId>::from_string("09280d25-d7a4-4270-b7e8-3219db436485").value();
  static constexpr auto name = "clockwork::MultiStateCogPolicy";
  static constexpr auto simulated_execution_duration = std::chrono::milliseconds(0);
  using MemoryResourcesType = CogMemoryResources<>;
  using ConfigsType = CogConfigs<>;
  using TimersType = CogTimers<>;
  using InputsType = CogInputs<>;
  using ConditionsType = CogConditions<>;
  using PublishersType = CogPublishers<>;
  using DiagnosticsType = CogDiagnostics<>;

  struct State1Policy
  {
    using StateType = TestState;
    static constexpr auto name = "State1Policy";
    static constexpr auto endpoint_id =
      jewels::Uuid<common::EndpointClassId>::from_string("bd47a9f5-2e61-4a83-bcfb-99581d75c15b").value();
    static constexpr bool read_only = false;
  };

  struct State2Policy
  {
    using StateType = TestState;
    static constexpr auto name = "State2Policy";
    static constexpr auto endpoint_id =
      jewels::Uuid<common::EndpointClassId>::from_string("0a46548b-c309-45a9-ab48-fb96c0b599de").value();
    static constexpr bool read_only = false;
  };

  using StatesType = CogStates<State1Policy, State2Policy>;

  [[nodiscard]] static bool is_ready(
    CogStatistics& /*statistics*/,
    typename TimersType::ConditionsTuple& /*timers*/,
    typename ConditionsType::ConditionsTuple& /*conditions*/)
  {
    return true;
  }

  // NOLINTNEXTLINE(readability-function-size) needs to match the signature of the make_dial function
  [[nodiscard]] static MultiStateCogDial make_dial(
    const CogExecuteParams& params,
    const typename MemoryResourcesType::MemoryResourcesTuple& /*resources*/,
    const typename ConfigsType::ConfigsTuple& /*configs*/,
    const typename StatesType::StatesTuple& states,
    typename InputsType::InputDialTuple /*inputs*/,
    typename PublishersType::PublishablesTuple /*publishables*/,
    typename TimersType::ConditionsTuple& /*timer_conditions*/,
    typename ConditionsType::ConditionsTuple& /*input_conditions*/,
    typename DiagnosticsType::ReporterType& /*diagnostics*/)
  {
    return MultiStateCogDial{
      .start_time = params.start_time,
      .state1 = jewels::memory::make_non_null_from_ref(*std::get<0>(states)),
      .state2 = jewels::memory::make_non_null_from_ref(*std::get<1>(states)),
    };
  }

  static void execute(MultiStateCogDial& dial)
  {
    ++dial.state1->counts.at(index);
    ++dial.state2->counts.at(index);
  }
};

TEST_CASE("multiple shared states", "[simple_cog]")
{
  constexpr auto duration = std::chrono::seconds(5);
  constexpr auto min_exe_count = 1;

  auto resource = jewels::memory::MemoryResource(std::pmr::new_delete_resource());
  auto queue = OnlineCogQueue(resource);

  auto state1 = std::make_shared<CogStateDataImpl<TestState>>(resource);
  auto state2 = std::make_shared<CogStateDataImpl<TestState>>(resource);
  auto cogs = [&resource, &queue]<size_t... idx>(std::index_sequence<idx...>)
  {
    return std::make_tuple(std::make_unique<SimpleCog<MultiStateCogPolicy<idx>>>(
      resource, jewels::Uuid<common::CogInstanceId>{}, jewels::memory::make_non_null_from_ref(queue))...);
  }(std::make_index_sequence<thread_count>{});
  REQUIRE(std::apply(
    [&state1, &state2](auto&... cog)
    {
      auto set1 = (cog->set_handle(MultiStateCogPolicy<0>::State1Policy::endpoint_id, state1, true) && ...);
      auto set2 = (cog->set_handle(MultiStateCogPolicy<0>::State2Policy::endpoint_id, state2, true) && ...);
      return (set1 && set2);
    },
    cogs));

  REQUIRE(std::apply([](const auto&... cog) { return (cog->validate() && ...); }, cogs));

  auto pool_config = ThreadPoolConfig{
    .resource = resource,
    .thread_configs = std::pmr::vector<ThreadConfig>(
      thread_count, ThreadConfig{.work = jewels::memory::make_non_null_from_ref(queue)}, resource),
  };
  auto pool = ThreadPool(pool_config);

  auto runner_config = OnlineRunnerConfig{
    .cogs = std::apply(
      [](auto&... cog)
      { return std::pmr::vector<CogConfig>({CogConfig{.cog = jewels::memory::make_non_null_from_ref(*cog)}...}); },
      cogs),
    .pool = jewels::memory::make_non_null_from_ref(pool)};
  auto runner = OnlineRunner(runner_config);

  // Notify all the cogs to start them.

  std::apply([](auto&... cog) { (cog->notify({}), ...); }, cogs);

  // Start the runner.

  runner.start();

  // Wait a few seconds.

  std::this_thread::sleep_for(duration);

  // Stop the runner.

  runner.stop();
  runner.join();

  // Verify no cogs were starved.

  REQUIRE(std::all_of(
    state1->state.counts.begin(),
    state1->state.counts.end(),
    [&min_exe_count](const auto& count) { return count > min_exe_count; }));

  REQUIRE(std::all_of(
    state2->state.counts.begin(),
    state2->state.counts.end(),
    [&min_exe_count](const auto& count) { return count > min_exe_count; }));
}

} // namespace
} // namespace clockwork
