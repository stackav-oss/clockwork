// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/cog_state.hh"
#include "clockwork/cog/cog_states.hh"
#include "clockwork/common/process_description.hh"
#include "jewels/container/compare.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_shared_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>
#include <gsl/util>

#include <cstdint>
#include <memory>
#include <memory_resource>
#include <string_view>
#include <tuple>
#include <type_traits>

namespace clockwork
{
namespace
{

struct TestState1
{
  explicit TestState1(jewels::memory::MemoryResource /*unused*/) {}

  int32_t value = {};
  bool operator==(const TestState1& rhs) const = default;
};

struct TestState2
{
  explicit TestState2(jewels::memory::MemoryResource /*unused*/) {}

  int32_t value = {};
  bool operator==(const TestState2& rhs) const = default;
};

class TestCog
{
public:
  void notify(jewels::time::SyncTime /*current_time*/) {}
};

template <typename... Policies>
struct CogStatesFixture // NOLINT(clang-analyzer-optin.performance.Padding) Test code so performance is not a concern.
{
  static constexpr auto policy_count = sizeof...(Policies);

  CogStatesFixture()
    : resource(std::pmr::new_delete_resource()), state(resource)
  {
  }

  jewels::memory::MemoryResource resource;
  TestCog cog;
  CogStates<Policies...> state;
};

struct StatePolicy1
{
  using StateType = TestState1;
  static constexpr auto endpoint_id =
    jewels::Uuid<common::EndpointClassId>::from_string("b6e2b628-62ba-4c73-b07e-b2ce77a742b4").value();
  static constexpr std::string_view name = "StatePolicy1";
  static constexpr auto read_only = true;
};

struct StatePolicy2
{
  using StateType = TestState2;
  static constexpr auto endpoint_id =
    jewels::Uuid<common::EndpointClassId>::from_string("789e340c-556f-4cf0-a3b9-73632ba75a7c").value();
  static constexpr std::string_view name = "StatePolicy2";
  static constexpr auto read_only = false;
};

using StatePolicyFixture = CogStatesFixture<StatePolicy1, StatePolicy2>;

TEST_CASE_METHOD(StatePolicyFixture, "basic operation", "[cog_states]")
{
  REQUIRE_FALSE(state.validate());

  // Set states
  const jewels::memory::MemoryResource memres(std::pmr::new_delete_resource());

  auto state1 = std::make_shared<CogStateDataImpl<TestState1>>(memres);
  auto state2 = std::make_shared<CogStateDataImpl<TestState2>>(memres);
  auto cog_ptr = jewels::memory::make_non_null_from_ref(cog);

  SECTION("set_handle unknown id")
  {
    constexpr auto unknown_id =
      jewels::Uuid<common::EndpointClassId>::from_string("b5e2c9a9-e351-4877-b5cc-77e76a7ebe63").value();
    REQUIRE_FALSE(state.set_handle(unknown_id, state1, true, cog_ptr));
  }

  SECTION("set_handle wrong type")
  {
    REQUIRE_FALSE(state.set_handle(StatePolicy1::endpoint_id, state2, true, cog_ptr));
  }

  REQUIRE_FALSE(state.validate());
  REQUIRE(state.set_handle(StatePolicy1::endpoint_id, state1, true, cog_ptr));
  REQUIRE(1 == state1->observers.size());
  REQUIRE_FALSE(state.validate());
  REQUIRE(state.set_handle(StatePolicy2::endpoint_id, state2, true, cog_ptr));
  REQUIRE(1 == state2->observers.size());
  REQUIRE(state.validate());

  // acquire locks

  REQUIRE_FALSE(state.is_locked());
  REQUIRE(state.try_lock());
  REQUIRE(state.is_locked());

  // get the states

  auto states = state.make_states();
  REQUIRE(2 == std::tuple_size<decltype(states)>());

  auto actual1 = std::get<0>(states);
  REQUIRE(actual1);
  REQUIRE(std::is_const_v<std::remove_reference_t<decltype(*actual1)>>);
  REQUIRE(state1->state == *actual1);

  auto actual2 = std::get<1>(states);
  REQUIRE(actual2);
  REQUIRE_FALSE(std::is_const_v<std::remove_reference_t<decltype(*actual2)>>);
  REQUIRE(state2->state == *actual2);

  // release locks

  REQUIRE_NOTHROW(state.unlock());
  REQUIRE_FALSE(state.is_locked());
}

using ZeroStatesPolicyFixture = CogStatesFixture<>;

TEST_CASE_METHOD(ZeroStatesPolicyFixture, "zero states", "[cog_states]")
{
  REQUIRE(state.validate());

  REQUIRE(state.is_locked());
  REQUIRE(state.try_lock());
  REQUIRE(state.is_locked());

  auto states = state.make_states(); // NOLINT(clang-analyzer-deadcode.DeadStores) needed for decltype.
  REQUIRE(0 == std::tuple_size<decltype(states)>());

  REQUIRE_NOTHROW(state.unlock());
  REQUIRE(state.is_locked());
}

} // namespace
} // namespace clockwork
