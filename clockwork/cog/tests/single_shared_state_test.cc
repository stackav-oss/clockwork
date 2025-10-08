// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/cog_state.hh"
#include "clockwork/cog/cog_states.hh"
#include "clockwork/cog/simple_cog.hh"
#include "clockwork/cog/tests/support/fake_cog.hh"
#include "clockwork/common/abstract_cog.hh"
#include "clockwork/common/process_description.hh"
#include "clockwork/runners/online_cog_queue.hh"
#include "clockwork/runners/online_runner.hh"
#include "clockwork/runners/thread_pool.hh"
#include "jewels/container/compare.hh"
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

struct SingleStateCogDial
{
  jewels::time::SyncTime start_time;
  jewels::memory::ObjectPtr<TestState> state;
};

/// An always ready test cog for testing state locking.
template <size_t index>
struct SingleStateCogPolicy : testing::FakeCogPolicy<0, 0>
{
  static constexpr auto cog_id =
    jewels::Uuid<common::EndpointClassId>::from_string("4989409f-7612-4cd1-96e4-3b37dd6c4cc4").value();
  static constexpr auto name = "clockwork:SingleStateCogPolicy";

  struct StatePolicy
  {
    using StateType = TestState;
    static constexpr auto name = "StatePolicy";
    static constexpr auto endpoint_id =
      jewels::Uuid<common::EndpointClassId>::from_string("0f966951-c3da-45db-87be-6328ea0fe3e7").value();
    static constexpr bool read_only = false;
  };

  using StatesType = CogStates<StatePolicy>;

  // NOLINTNEXTLINE(readability-function-size) Must match the signature of the make_dial function.
  [[nodiscard]] static SingleStateCogDial make_dial(
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
    return SingleStateCogDial{
      .start_time = params.start_time,
      .state = jewels::memory::make_non_null_from_ref(*std::get<0>(states)),
    };
  }

  static void execute(SingleStateCogDial& dial)
  {
    ++dial.state->counts.at(index);
  }
};

TEST_CASE("single shared state", "[simple_cog]")
{
  constexpr auto duration = std::chrono::seconds(5);
  constexpr auto min_exe_count = 1;

  auto resource = jewels::memory::MemoryResource(std::pmr::new_delete_resource());
  auto queue = OnlineCogQueue(resource);

  auto state = std::make_shared<CogStateDataImpl<TestState>>(resource);
  auto cogs = [&resource, &queue]<size_t... idx>(std::index_sequence<idx...>)
  {
    return std::make_tuple(
      std::make_unique<SimpleCog<SingleStateCogPolicy<idx>>>(
        resource, jewels::Uuid<common::CogInstanceId>{}, jewels::memory::make_non_null_from_ref(queue))...);
  }(std::make_index_sequence<thread_count>{});
  REQUIRE(
    std::apply(
      [&state](auto&... cog)
      { return (cog->set_handle(SingleStateCogPolicy<0>::StatePolicy::endpoint_id, state, true) && ...); },
      cogs));

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

  REQUIRE(
    std::all_of(
      state->state.counts.begin(),
      state->state.counts.end(),
      [&min_exe_count](const auto& count) { return count > min_exe_count; }));
}

} // namespace
} // namespace clockwork
