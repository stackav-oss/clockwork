// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/cog_state.hh"
#include "clockwork/cog/tests/support/mock_metrics_schemas_clk_cc.hh"
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/in_memory_channel.hh"
#include "clockwork/pinion/slot.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/container/compare.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/memory/pointers.hh"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <span>
#include <type_traits>
#include <utility>

namespace clockwork
{
namespace
{

using jewels::ok;

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

TEST_CASE_METHOD(StatePolicyFixture, "set_from_bytes on C++ state fails", "[cog_state]")
{
  // C++ states do not support set_from_bytes - it should return failure
  std::array<std::byte, sizeof(TestState)> data{};
  REQUIRE(jewels::fails(record->set_from_bytes(data)));
}

TEST_CASE("set_from_bytes on schema-based state succeeds", "[cog_state]")
{
  using clockwork::cog::metrics::test::TestCogTelemetryMetrics;

  // Create an in-memory channel for the schema-based state
  InMemoryChannel<Tap<Tachyon<TestCogTelemetryMetrics>>, 1, false> channel{
    jewels::memory::MemoryResource{std::pmr::new_delete_resource()}};
  auto publisher_handle = channel.make_publisher(0);

  // Create a CogStateDataImpl for the schema-based state
  auto state = CogStateDataImpl<Tap<Tachyon<TestCogTelemetryMetrics>>>{std::move(publisher_handle)};

  // Get the actual size of the state message from the publisher
  REQUIRE(state.current_publishable.has_value());
  auto& message_ref = state.current_publishable.value().message();
  const auto message_size = sizeof(message_ref);

  // Create some test data - use a buffer large enough for the schema
  std::array<std::byte, message_size> temp_data{};
  // Fill with some non-zero pattern (only up to actual message size)
  for (size_t i = 0; i < temp_data.size(); ++i)
  {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) Test code/data only
    temp_data[i] = static_cast<std::byte>(i % 256);
  }
  std::span<const std::byte> test_data{temp_data};

  // Call set_from_bytes
  auto outcome = state.set_from_bytes(test_data);
  REQUIRE(ok(outcome));

  // Verify the data was copied - check that buffer is not all zeros
  REQUIRE(state.current_publishable.has_value());
  auto& state_ref = state.current_publishable.value().message();
  auto bytes_span = std::as_bytes(std::span{&state_ref, 1});
  CHECK(std::ranges::equal(bytes_span, test_data));
}

TEST_CASE("set_from_bytes with wrong size fails", "[cog_state]")
{
  using clockwork::cog::metrics::test::TestCogTelemetryMetrics;

  // Create an in-memory channel for the schema-based state
  InMemoryChannel<Tap<Tachyon<TestCogTelemetryMetrics>>, 1, false> channel{
    jewels::memory::MemoryResource{std::pmr::new_delete_resource()}};
  auto publisher_handle = channel.make_publisher(0);

  // Create a CogStateDataImpl for the schema-based state
  auto state = CogStateDataImpl<Tap<Tachyon<TestCogTelemetryMetrics>>>{std::move(publisher_handle)};

  // Create data with wrong size (too small)
  std::array<std::byte, 10> wrong_size_data{};

  // Call set_from_bytes with wrong size - should fail
  auto outcome = state.set_from_bytes(wrong_size_data);
  REQUIRE(outcome.fails());
}

} // namespace
} // namespace clockwork
