// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/include_common.hh"
#include "clockwork/dsl/tests/support/clk_hellocog_clk_cc.hh"
#include "clockwork/dsl/tests/support/clk_hellomsg_clk_cc.hh"
#include "clockwork/dsl/tests/support/goodbyecog.hh"
#include "clockwork/tags.hh"
#include "jewels/memory/memory_resource.hh"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <concepts>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace clockwork::testing::concepts
{

// Concept for basic cog policy requirements
template <typename T>
concept CogPolicy = requires(
  CogStatistics& stats,
  typename T::TimersType::ConditionsTuple& timers,
  typename T::ConditionsType::ConditionsTuple& conditions,
  const CogExecuteParams& params,
  typename T::MemoryResourcesType::MemoryResourcesTuple& resources,
  typename T::ConfigsType::ConfigsTuple& configs,
  typename T::StatesType::StatesTuple& states,
  typename T::InputsType::InputDialTuple& inputs,
  typename T::PublishersType::PublishablesTuple publishables,
  typename T::DiagnosticsType::ReporterType& diagnostics,
  typename T::SignalApiType& signals,
  typename T::CogDial& dial) {
  // Type requirements
  { T::name } -> std::convertible_to<std::string_view>;
  { T::simulated_execution_duration } -> std::convertible_to<std::chrono::milliseconds>;
  { T::cog_id } -> std::convertible_to<jewels::Uuid<clockwork::common::CogClassId>>;
  typename T::CogDial;
  typename T::MemoryResourcesType;
  typename T::ConfigsType;
  typename T::StatesType;
  typename T::TimersType;
  typename T::InputsType;
  typename T::ConditionsType;
  typename T::PublishersType;
  typename T::DiagnosticsType;

  // Function requirements
  { T::is_ready(stats, timers, conditions) } -> std::same_as<bool>;
  {
    T::make_dial(params, resources, configs, states, inputs, publishables, timers, conditions, diagnostics, signals)
  } -> std::same_as<typename T::CogDial>;
  { T::execute(dial) } -> std::same_as<void>;
};

// Concept for memory resource policy
template <typename T>
concept MemoryResourcePolicy = requires {
  typename T::MemoryResourceType;
  { T::endpoint_id } -> std::convertible_to<jewels::Uuid<clockwork::common::EndpointClassId>>;
  { T::name } -> std::convertible_to<std::string_view>;
};

// Concept for config policy
template <typename T>
concept ConfigPolicy = requires {
  typename T::ConfigType;
  { T::endpoint_id } -> std::convertible_to<jewels::Uuid<clockwork::common::EndpointClassId>>;
  { T::name } -> std::convertible_to<std::string_view>;
};

// Concept for state policy
template <typename T>
concept StatePolicy = requires {
  typename T::StateType;
  typename T::Factory;
  { T::endpoint_id } -> std::convertible_to<jewels::Uuid<clockwork::common::EndpointClassId>>;
  { T::read_only } -> std::convertible_to<bool>;
  { T::name } -> std::convertible_to<std::string_view>;
};

// Concept for timer policy
template <typename T>
concept TimerPolicy = requires {
  { T::threshold_ns } -> std::convertible_to<int64_t>;
  { T::endpoint_id } -> std::convertible_to<jewels::Uuid<clockwork::common::EndpointClassId>>;
  { T::name } -> std::convertible_to<std::string_view>;
};

// Concept for input policy
template <typename T>
concept InputPolicy = requires {
  typename T::MsgType;
  { T::endpoint_id } -> std::convertible_to<jewels::Uuid<clockwork::common::EndpointClassId>>;
  { T::name } -> std::convertible_to<std::string_view>;
  { T::max_view_size } -> std::convertible_to<unsigned int>;
  { T::copy_inputs } -> std::convertible_to<bool>;
  { T::manual_cursor } -> std::convertible_to<bool>;
  { T::expose_seqno } -> std::convertible_to<bool>;
  { T::use_device_ptr } -> std::convertible_to<bool>;
};

// Concept for condition policy
template <typename T>
concept ConditionPolicy = requires {
  { T::endpoint_id } -> std::convertible_to<jewels::Uuid<clockwork::common::EndpointClassId>>;
  { T::name } -> std::convertible_to<std::string_view>;
  { T::bounds_min } -> std::convertible_to<unsigned int>;
  { T::bounds_max } -> std::convertible_to<unsigned int>;
  { T::condition_type } -> std::convertible_to<clockwork::InputConditionType>;
};

// Concept for publisher policy
template <typename T>
concept PublisherPolicy = requires {
  typename T::MsgType;
  { T::endpoint_id } -> std::convertible_to<jewels::Uuid<clockwork::common::EndpointClassId>>;
  { T::name } -> std::convertible_to<std::string_view>;
};

// Concept for diagnostics policy
template <typename T>
concept DiagnosticsPolicy = requires {
  typename T::ManagerType;
  { T::endpoint_id } -> std::convertible_to<jewels::Uuid<clockwork::common::EndpointClassId>>;
  { T::name } -> std::convertible_to<std::string_view>;
  { T::member_name } -> std::convertible_to<std::string_view>;
  { T::group_name } -> std::convertible_to<std::string_view>;
  { T::instance_name } -> std::convertible_to<std::string_view>;
};

// Concept for cog factory
template <typename T>
concept CogFactory = std::derived_from<T, clockwork::CogFactory> &&
                     requires {
                       { T::type_id } -> std::convertible_to<jewels::Uuid<clockwork::common::CogClassId>>;
                     } &&
                     requires(
                       const T& factory,
                       jewels::memory::MemoryResource resource,
                       const jewels::Uuid<clockwork::common::CogInstanceId>& instance_id,
                       jewels::memory::ObjectPtr<clockwork::AbstractCogQueue> queue) {
                       { factory.id() } -> std::same_as<const clockwork::CogFactory::IdType&>;
                       { factory.make(resource, instance_id, queue) } -> std::same_as<clockwork::CogFactory::Ptr>;
                     };

// Concept for state factory
template <typename T>
concept StateFactory = std::derived_from<T, clockwork::CogStateFactory> && requires {
  { T::type_id } -> std::convertible_to<jewels::Uuid<clockwork::RepresentationTag>>;
} && requires(const T& factory, jewels::memory::MemoryResource memres) {
  { factory.id() } -> std::same_as<const clockwork::CogStateFactory::IdType&>;
};

} // namespace clockwork::testing::concepts

namespace clockwork::cogs
{

TEST_CASE("GoodbyeCogPolicy satisfies CogPolicy concept", "[cppcog][goodbyecog]")
{
  static_assert(clockwork::testing::concepts::CogPolicy<GoodbyeCogPolicy>);

  // Test specific properties for empty cog
  static_assert(std::is_same_v<GoodbyeCogPolicy::MemoryResourcesType, clockwork::CogMemoryResources<>>);
  static_assert(std::is_same_v<GoodbyeCogPolicy::ConfigsType, clockwork::CogConfigs<>>);
  static_assert(std::is_same_v<GoodbyeCogPolicy::StatesType, clockwork::CogStates<>>);
  static_assert(std::is_same_v<GoodbyeCogPolicy::TimersType, clockwork::CogTimers<>>);
  static_assert(std::is_same_v<GoodbyeCogPolicy::InputsType, clockwork::CogInputs<>>);
  static_assert(std::is_same_v<GoodbyeCogPolicy::ConditionsType, clockwork::CogConditions<>>);
  static_assert(std::is_same_v<GoodbyeCogPolicy::PublishersType, clockwork::CogPublishers<>>);
  static_assert(std::is_same_v<GoodbyeCogPolicy::DiagnosticsType, clockwork::CogDiagnostics<>>);
}

TEST_CASE("GoodbyeCog type alias exists")
{
  static_assert(std::is_same_v<GoodbyeCog, clockwork::SimpleCog<GoodbyeCogPolicy>>);
}

TEST_CASE("GoodbyeCogFactory satisfies CogFactory concept")
{
  static_assert(clockwork::testing::concepts::CogFactory<GoodbyeCogFactory>);
}

} // namespace clockwork::cogs

namespace clockwork::testing::cogs
{

TEST_CASE("HelloCogPolicy satisfies CogPolicy concept")
{
  static_assert(clockwork::testing::concepts::CogPolicy<HelloCogPolicy>);
}

TEST_CASE("HelloCogPolicy sub-policies satisfy their concepts")
{
  // Memory resources
  static_assert(clockwork::testing::concepts::MemoryResourcePolicy<HelloCogPolicy::MemHelloPolicy>);

  // Configs
  static_assert(clockwork::testing::concepts::ConfigPolicy<HelloCogPolicy::CfgHelloPolicy>);

  // States
  static_assert(clockwork::testing::concepts::StatePolicy<HelloCogPolicy::RoHelloPolicy>);
  static_assert(clockwork::testing::concepts::StatePolicy<HelloCogPolicy::RwHelloPolicy>);
  static_assert(clockwork::testing::concepts::StatePolicy<HelloCogPolicy::ExternHelloPolicy>);
  static_assert(
    std::is_same_v<HelloCogPolicy::ExternHelloPolicy::SerializedType, clockwork::Tappy<clockwork::demo::HelloMsg>>);

  // Timers
  static_assert(clockwork::testing::concepts::TimerPolicy<HelloCogPolicy::PeriodicPolicy>);

  // Inputs
  static_assert(clockwork::testing::concepts::InputPolicy<HelloCogPolicy::LatestHelloPolicy>);
  static_assert(clockwork::testing::concepts::InputPolicy<HelloCogPolicy::HistoryOfHellosPolicy>);

  // Conditions
  static_assert(clockwork::testing::concepts::ConditionPolicy<HelloCogPolicy::AnyMsgPolicy>);
  static_assert(clockwork::testing::concepts::ConditionPolicy<HelloCogPolicy::NewMsgPolicy>);

  // Publishers
  static_assert(clockwork::testing::concepts::PublisherPolicy<HelloCogPolicy::OutWorldPolicy>);
  static_assert(clockwork::testing::concepts::PublisherPolicy<HelloCogPolicy::OutGoodbyePolicy>);
  static_assert(clockwork::testing::concepts::PublisherPolicy<HelloCogPolicy::OutMulti1Policy>);
  static_assert(clockwork::testing::concepts::PublisherPolicy<HelloCogPolicy::OutMulti2Policy>);

  // Diagnostics
  static_assert(clockwork::testing::concepts::DiagnosticsPolicy<HelloCogPolicy::DiagnosticsPolicy>);
}

TEST_CASE("HelloCog type alias exists")
{
  static_assert(std::is_same_v<HelloCog, clockwork::SimpleCog<HelloCogPolicy>>);
}

TEST_CASE("HelloCogFactory satisfies CogFactory concept")
{
  static_assert(clockwork::testing::concepts::CogFactory<HelloCogFactory>);
}

TEST_CASE("HelloCog State Factories satisfy StateFactory concept")
{
  static_assert(clockwork::testing::concepts::StateFactory<HelloCogPolicy::RoHelloPolicy::Factory>);
  static_assert(clockwork::testing::concepts::StateFactory<HelloCogPolicy::RwHelloPolicy::Factory>);
  static_assert(clockwork::testing::concepts::StateFactory<HelloCogPolicy::ExternHelloPolicy::Factory>);
}

} // namespace clockwork::testing::cogs
