// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/cog_state.hh"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <type_traits>

namespace clockwork
{
namespace
{

struct TestState
{
  explicit TestState(jewels::memory::MemoryResource /*unused*/) {}

  int32_t value = {};
  bool operator==(const TestState& rhs) const = default;
};

template <typename PolicyType>
struct CogStateFixture // NOLINT(clang-analyzer-optin.performance.Padding) Test code so performance is not a concern.
{
  using Policy = PolicyType;
  using StateType = typename Policy::StateType;
  using CogStateType = CogState<Policy>;
  using RecordType = typename CogStateType::RecordType;
  using RecordPtrType = typename CogStateType::RecordPtrType;

  CogStateFixture()
    : memres(std::pmr::new_delete_resource()), record(std::make_shared<RecordType>(memres)), cog_state(record)
  {
  }

  jewels::memory::MemoryResource memres;
  RecordPtrType record;
  CogStateType cog_state;
};

struct StatePolicy
{
  using StateType = TestState;
  static constexpr auto read_only = false;
};

using StatePolicyFixture = CogStateFixture<StatePolicy>;

TEST_CASE_METHOD(StatePolicyFixture, "const state", "[cog_state]")
{
  REQUIRE(cog_state.validate());

  REQUIRE_FALSE(cog_state.is_locked());
  REQUIRE(cog_state.try_lock());
  REQUIRE(cog_state.is_locked());

  auto state = cog_state.get_state();
  REQUIRE(*state == record->state);
  REQUIRE_FALSE(std::is_const_v<std::remove_reference_t<decltype(*state)>>);

  REQUIRE_NOTHROW(cog_state.unlock());
  REQUIRE_FALSE(cog_state.is_locked());
}

struct ConstStatePolicy
{
  using StateType = TestState;
  static constexpr auto read_only = true;
};

using ConstStatePolicyFixture = CogStateFixture<ConstStatePolicy>;

TEST_CASE_METHOD(ConstStatePolicyFixture, "const state", "[cog_state]")
{
  REQUIRE(cog_state.validate());

  REQUIRE_FALSE(cog_state.is_locked());
  REQUIRE(cog_state.try_lock());
  REQUIRE(cog_state.is_locked());

  auto state = cog_state.get_state();
  REQUIRE(*state == record->state);
  REQUIRE(std::is_const_v<std::remove_reference_t<decltype(*state)>>);

  REQUIRE_NOTHROW(cog_state.unlock());
  REQUIRE_FALSE(cog_state.is_locked());
}

} // namespace
} // namespace clockwork
