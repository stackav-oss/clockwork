// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/cog_state.hh"
#include "clockwork/cog/cog_states.hh"
#include "clockwork/cog/tests/support/unit_test_states_clk_cc.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "clockwork/pinion/shm_channel_factory.hh"
#include "clockwork/pinion/shm_publisher.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/container/compare.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_shared_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>
#include <gsl/util>

#include <cstdint>
#include <functional>
#include <memory>
#include <memory_resource>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace clockwork::cogs::testing
{
namespace
{

using jewels::FactoryResult;
using jewels::ok;
using jewels::Out;

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
  using StateType = CxxState;
  static constexpr auto endpoint_id =
    jewels::Uuid<common::EndpointClassId>::from_string("b6e2b628-62ba-4c73-b07e-b2ce77a742b4").value();
  static constexpr std::string_view name = "StatePolicy1";
  static constexpr auto read_only = true;
};

struct StatePolicy2
{
  using StateType = Tappy<ClkState>;
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
  jewels::testing::TmpDirectoryGuard temp_dir;
  const auto shm_dir = temp_dir.get_path().string();
  const auto nmsp = jewels::Uuid<void>::random_uuid().to_string();
  auto shm_channel_factory = clockwork::pinion::ShmChannelFactory::make(memres, nmsp, shm_dir);
  REQUIRE(shm_channel_factory);
  const auto channel_uuid = jewels::Uuid<void>::random_uuid().to_string();
  const pinion::BufferLayout layout{
    .num_slots = 1,
    .message_size = sizeof(Tappy<ClkState>),
    .is_published_once = false,
  };
  auto publisher = shm_channel_factory->open_publisher(channel_uuid, "state", layout, 1);
  REQUIRE(publisher);
  auto publisher_handle = publisher.value()->extract_publisher();
  REQUIRE(publisher_handle);

  auto state_ptr1 = std::make_shared<CogStateDataImpl<CxxState>>(memres);
  state_ptr1->get_ptr()->value = 1;
  auto state_ptr2 = std::make_shared<CogStateDataImpl<Tappy<ClkState>>>(std::move(publisher_handle).value());
  state_ptr2->get_ptr()->set_value(2);
  auto cog_ptr = jewels::memory::make_non_null_from_ref(cog);

  SECTION("set_handle unknown id")
  {
    constexpr auto unknown_id =
      jewels::Uuid<common::EndpointClassId>::from_string("b5e2c9a9-e351-4877-b5cc-77e76a7ebe63").value();
    REQUIRE_FALSE(state.set_handle(unknown_id, state_ptr1, true, cog_ptr));
  }

  SECTION("set_handle wrong type")
  {
    REQUIRE_FALSE(state.set_handle(StatePolicy1::endpoint_id, state_ptr2, true, cog_ptr));
  }

  SECTION("set_handle")
  {
    REQUIRE_FALSE(state.validate());
    REQUIRE(state.set_handle(StatePolicy1::endpoint_id, state_ptr1, true, cog_ptr));
    REQUIRE(1 == state_ptr1->observers.size());
    REQUIRE_FALSE(state.validate());
    REQUIRE(state.set_handle(StatePolicy2::endpoint_id, state_ptr2, true, cog_ptr));
    REQUIRE(1 == state_ptr2->observers.size());
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
    REQUIRE(state_ptr1->state == *actual1);

    auto actual2 = std::get<1>(states);
    REQUIRE(actual2);
    REQUIRE_FALSE(std::is_const_v<std::remove_reference_t<decltype(*actual2)>>);
    REQUIRE(*(state_ptr2->get_ptr()) == *actual2);

    // release locks

    REQUIRE_NOTHROW(state.unlock());
    REQUIRE_FALSE(state.is_locked());
  }

  SECTION("unit test helpers")
  {
    CogStates<StatePolicy1, StatePolicy2> state{memres};
    REQUIRE_FALSE(state.is_state_set<0>());
    REQUIRE_FALSE(state.is_state_set<1>());

    const auto channel_uuid2 = jewels::Uuid<void>::random_uuid().to_string();
    auto publisher2 = shm_channel_factory->open_publisher(channel_uuid2, "state_ptr2", layout, 1);
    REQUIRE(publisher2);
    auto publisher_handle2 = publisher2.value()->extract_publisher();
    REQUIRE(publisher_handle2);

    REQUIRE(ok(state.initialize_state<0>()));
    REQUIRE(state.is_state_set<0>());
    REQUIRE_FALSE(state.is_state_set<1>());
    REQUIRE(ok(state.initialize_state<1>(std::move(publisher_handle2).value())));
    REQUIRE(state.is_state_set<0>());
    REQUIRE(state.is_state_set<1>());

    jewels::FactoryResult<std::reference_wrapper<CxxState>> value1;
    REQUIRE(ok(state.get_state<0>(Out{value1})));
    REQUIRE(value1->get().value == 0);
    jewels::FactoryResult<std::reference_wrapper<Tappy<ClkState>>> value2;
    REQUIRE(ok(state.get_state<1>(Out{value2})));
    REQUIRE(value2->get().get_value() == 0);

    value1->get() = *(state_ptr1->get_ptr());
    value2->get() = *(state_ptr2->get_ptr());

    CogStates<StatePolicy1, StatePolicy2> state2{memres};
    REQUIRE_FALSE(state2.is_state_set<0>());
    REQUIRE_FALSE(state2.is_state_set<1>());

    std::shared_ptr<CogStateDataImpl<CxxState>> cog_state_handle1;
    REQUIRE(ok(state.get_state_handle<0>(Out{cog_state_handle1})));
    REQUIRE(ok(state2.set_state_handle<0>(cog_state_handle1)));
    REQUIRE(state2.is_state_set<0>());
    REQUIRE_FALSE(state2.is_state_set<1>());
    std::shared_ptr<CogStateDataImpl<Tappy<ClkState>>> cog_state_handle2;
    REQUIRE(ok(state.get_state_handle<1>(Out{cog_state_handle2})));
    REQUIRE(ok(state2.set_state_handle<1>(cog_state_handle2)));
    REQUIRE(state2.is_state_set<0>());
    REQUIRE(state2.is_state_set<1>());

    jewels::FactoryResult<std::reference_wrapper<CxxState>> value3;
    REQUIRE(ok(state2.get_state<0>(Out{value3})));
    REQUIRE(*(state_ptr1->get_ptr()) == value3->get());
    jewels::FactoryResult<std::reference_wrapper<Tappy<ClkState>>> value4;
    REQUIRE(ok(state2.get_state<1>(Out{value4})));
    REQUIRE(*(state_ptr2->get_ptr()) == value4->get());
  }
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
} // namespace clockwork::cogs::testing
