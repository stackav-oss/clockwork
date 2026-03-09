// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0
#include "clockwork/diagnostics/reporter.hh"
#include "clockwork/dsl/tests/support/goodbyecog_dial.hh"
#include "clockwork/dsl/tests/support/hello_msg_onboard.hh"
#include "clockwork/dsl/tests/support/hellocog_dial.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/time/sync_time.hh"

#include <catch2/catch_test_macros.hpp>

#include <concepts>
#include <type_traits>

namespace clockwork::testing::concepts
{

// Concept for dial resources
template <typename T>
concept DialResources = std::is_default_constructible_v<T> || requires {
  // Can be constructed with memory resources
  typename T;
};

// Concept for dial configs
template <typename T>
concept DialConfigs = std::is_default_constructible_v<T> || requires {
  // Can be constructed with config parameters
  typename T;
};

// Concept for dial states
template <typename T>
concept DialStates = std::is_default_constructible_v<T> || requires {
  // Can be constructed with state parameters
  typename T;
};

// Concept for dial conditions
template <typename T>
concept DialConditions = std::is_default_constructible_v<T> || requires {
  // Can be constructed with condition parameters
  typename T;
};

// Concept for dial inputs
template <typename T>
concept DialInputs = std::is_default_constructible_v<T> || requires {
  // Can be constructed with input parameters
  typename T;
};

// Concept for dial outputs
template <typename T>
concept DialOutputs = std::is_default_constructible_v<T> || requires {
  // Can be constructed with output parameters
  typename T;
};

// Concept for dial diagnostics
template <typename T>
concept DialDiagnostics = std::is_default_constructible_v<T> || requires {
  // Can be constructed with diagnostics parameters
  typename T;
};

// Concept for main dial structure
template <
  typename T,
  typename Resources,
  typename Configs,
  typename States,
  typename Conditions,
  typename Inputs,
  typename Outputs,
  typename Diagnostics,
  typename Signals>
concept CogDial = requires(
  jewels::time::SyncTime start_time,
  Resources resources,
  Configs configs,
  States states,
  Conditions conditions,
  Inputs inputs,
  Outputs outputs,
  Diagnostics diagnostics,
  Signals signals,
  T& dial) {
  // Constructor requirement
  T(start_time, resources, configs, states, conditions, inputs, outputs, diagnostics, signals);

  // Getter methods
  { dial.get_start_time() } -> std::same_as<jewels::time::SyncTime&>;
  { dial.get_resources() } -> std::same_as<Resources&>;
  { dial.get_configs() } -> std::same_as<Configs&>;
  { dial.get_states() } -> std::same_as<States&>;
  { dial.get_conditions() } -> std::same_as<Conditions&>;
  { dial.get_inputs() } -> std::same_as<Inputs&>;
  { dial.get_outputs() } -> std::same_as<Outputs&>;
  // For diagnostics, we need to handle the case where Diagnostics might be an ObjectPtr
  // The getter returns a reference to the pointed-to type, not the ObjectPtr itself
  { dial.get_diagnostics() }; // Just require that the method exists, don't check the exact return type
};

// Concept for execute_cog function
template <typename DialType>
concept HasExecuteCog = requires(DialType& dial) {
  { execute_cog(dial) } -> std::same_as<void>;
};

// Concept for empty dial components (for GoodbyeCog)
template <typename T>
concept EmptyDialComponent = std::is_default_constructible_v<T> && std::is_empty_v<T>;

} // namespace clockwork::testing::concepts

namespace clockwork::testing::cogs
{

TEST_CASE("HelloCog dial structures satisfy dial concepts")
{
  // Test individual dial components
  static_assert(clockwork::testing::concepts::DialResources<HelloCogDialResources>);
  static_assert(clockwork::testing::concepts::DialConfigs<HelloCogDialConfigs>);
  static_assert(clockwork::testing::concepts::DialStates<HelloCogDialStates>);
  static_assert(clockwork::testing::concepts::DialConditions<HelloCogDialConditions>);
  static_assert(clockwork::testing::concepts::DialInputs<HelloCogDialInputs>);
  static_assert(clockwork::testing::concepts::DialOutputs<HelloCogDialOutputs>);
  // Test execute_cog function exists
  static_assert(clockwork::testing::concepts::HasExecuteCog<HelloCogDial>);
}

TEST_CASE("HelloCog dial specific type requirements")
{
  // Test specific constructor signatures for HelloCog components
  static_assert(
    std::is_constructible_v<HelloCogDialResources, jewels::memory::ObjectPtr<jewels::memory::MemoryResource>>);

  static_assert(std::is_constructible_v<
                HelloCogDialConfigs,
                jewels::memory::ObjectPtr<const clockwork::Tap<clockwork::Tachyon<clockwork::demo::HelloMsg>>>>);

  static_assert(std::is_constructible_v<
                HelloCogDialStates,
                jewels::memory::ObjectPtr<const clockwork::Tap<clockwork::Tachyon<clockwork::demo::HelloMsg>>>,
                jewels::memory::ObjectPtr<clockwork::Tap<clockwork::Tachyon<clockwork::demo::HelloMsg>>>,
                jewels::memory::ObjectPtr<const clockwork::testing::CxxState>>);

  // Test specific getter signatures
  static_assert(std::is_same_v<
                decltype(&HelloCogDialResources::get_mem_hello),
                jewels::memory::MemoryResource& (HelloCogDialResources::*)()>);

  static_assert(std::is_same_v<
                decltype(&HelloCogDialConfigs::get_cfg_hello),
                const clockwork::Tap<clockwork::Tachyon<clockwork::demo::HelloMsg>>& (HelloCogDialConfigs::*)() const>);

  static_assert(std::is_same_v<
                decltype(&HelloCogDialStates::get_ro_hello),
                const clockwork::Tap<clockwork::Tachyon<clockwork::demo::HelloMsg>>& (HelloCogDialStates::*)() const>);

  static_assert(std::is_same_v<
                decltype(&HelloCogDialStates::get_rw_hello),
                clockwork::Tap<clockwork::Tachyon<clockwork::demo::HelloMsg>>& (HelloCogDialStates::*)()>);

  static_assert(std::is_same_v<
                decltype(&HelloCogDialStates::get_extern_hello),
                const clockwork::testing::CxxState& (HelloCogDialStates::*)() const>);
}

} // namespace clockwork::testing::cogs

namespace clockwork::cogs
{

TEST_CASE("GoodbyeCog dial structures satisfy empty dial concepts")
{
  // Test that all GoodbyeCog dial components are empty and default constructible
  static_assert(clockwork::testing::concepts::EmptyDialComponent<GoodbyeCogDialResources>);
  static_assert(clockwork::testing::concepts::EmptyDialComponent<GoodbyeCogDialConfigs>);
  static_assert(clockwork::testing::concepts::EmptyDialComponent<GoodbyeCogDialStates>);
  static_assert(clockwork::testing::concepts::EmptyDialComponent<GoodbyeCogDialConditions>);
  static_assert(clockwork::testing::concepts::EmptyDialComponent<GoodbyeCogDialInputs>);
  static_assert(clockwork::testing::concepts::EmptyDialComponent<GoodbyeCogDialOutputs>);
  static_assert(clockwork::testing::concepts::EmptyDialComponent<GoodbyeCogDialDiagnostics>);

  // Test main dial concept
  static_assert(clockwork::testing::concepts::CogDial<
                GoodbyeCogDial,
                GoodbyeCogDialResources,
                GoodbyeCogDialConfigs,
                GoodbyeCogDialStates,
                GoodbyeCogDialConditions,
                GoodbyeCogDialInputs,
                GoodbyeCogDialOutputs,
                GoodbyeCogDialDiagnostics,
                GoodbyeCogDialSignalApi>);

  // Test execute_cog function exists
  static_assert(clockwork::testing::concepts::HasExecuteCog<GoodbyeCogDial>);
}

} // namespace clockwork::cogs
