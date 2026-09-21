// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/cog_configs.hh"
#include "clockwork/cog/detail.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "jewels/container/compare.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>
#include <gsl/util>

#include <cstdint>
#include <memory>
#include <string_view>
#include <tuple>

namespace clockwork
{
namespace
{

struct TestConfig1
{
  int32_t value = {};
};

struct TestConfig2
{
  int32_t value = {};
};

bool operator==(const TestConfig1& lhs, const TestConfig1& rhs)
{
  return (lhs.value == rhs.value);
}

bool operator==(const TestConfig2& lhs, const TestConfig2& rhs)
{
  return (lhs.value == rhs.value);
}

template <typename... Policies>
struct CogConfigsFixture // NOLINT(clang-analyzer-optin.performance.Padding). Test code performance is not a concern.
{
  static constexpr auto policy_count = sizeof...(Policies);

  CogConfigs<Policies...> config;
};

struct ConfigPolicy1
{
  using ConfigType = TestConfig1;
  static constexpr auto endpoint_id =
    jewels::Uuid<common::EndpointClassId>::from_string("b6e2b628-62ba-4c73-b07e-b2ce77a742b4").value();
  static constexpr std::string_view name = "ConfigPolicy1";
};

struct ConfigPolicy2
{
  using ConfigType = TestConfig2;
  static constexpr auto endpoint_id =
    jewels::Uuid<common::EndpointClassId>::from_string("789e340c-556f-4cf0-a3b9-73632ba75a7c").value();
  static constexpr std::string_view name = "ConfigPolicy2";
};

using ConfigPolicyFixture = CogConfigsFixture<ConfigPolicy1, ConfigPolicy2>;

TEST_CASE_METHOD(ConfigPolicyFixture, "basic operation", "[cog_configs]")
{
  REQUIRE_FALSE(config.validate());

  // Set configs

  auto config1 =
    std::make_shared<CogConfigDataImpl<TestConfig1>>(std::make_shared<TestConfig1>(TestConfig1{.value = 1}));
  auto config2 =
    std::make_shared<CogConfigDataImpl<TestConfig2>>(std::make_shared<TestConfig2>(TestConfig2{.value = 2}));

  SECTION("set_handle unknown id")
  {
    constexpr auto unknown_id =
      jewels::Uuid<common::EndpointClassId>::from_string("b5e2c9a9-e351-4877-b5cc-77e76a7ebe63").value();
    REQUIRE_FALSE(config.set_handle(unknown_id, config1));
  }

  SECTION("set_handle wrong type")
  {
    REQUIRE_FALSE(config.set_handle(ConfigPolicy1::endpoint_id, config2));
  }

  SECTION("set_handle")
  {
    REQUIRE_FALSE(config.validate());
    REQUIRE(config.set_handle(ConfigPolicy1::endpoint_id, config1));
    REQUIRE_FALSE(config.validate());
    REQUIRE(config.set_handle(ConfigPolicy2::endpoint_id, config2));
    REQUIRE(config.validate());

    auto configs = config.make_configs();
    REQUIRE(2 == std::tuple_size<decltype(configs)>());

    const auto& actual1 = std::get<0>(configs);
    REQUIRE(*config1->data == actual1);
    REQUIRE(config1->data.get() == &actual1);
    const auto& actual2 = std::get<1>(configs);
    REQUIRE(*config2->data == actual2);
    REQUIRE(config2->data.get() == &actual2);
  }

  SECTION("set_config/set_config_handle/get_config_handle")
  {
    REQUIRE_FALSE(config.validate());
    REQUIRE_FALSE(config.is_config_set<0>());
    REQUIRE_FALSE(config.is_config_set<1>());
    REQUIRE(jewels::ok(config.set_config_handle<0>(config1->data)));
    REQUIRE_FALSE(config.validate());
    REQUIRE(config.is_config_set<0>());
    REQUIRE_FALSE(config.is_config_set<1>());
    REQUIRE(jewels::ok(config.set_config_handle<1>(config2->data)));
    REQUIRE(config.validate());
    REQUIRE(config.is_config_set<0>());
    REQUIRE(config.is_config_set<1>());

    REQUIRE(jewels::fails(config.set_config_handle<0>(config1->data)));
    REQUIRE(jewels::fails(config.set_config_handle<1>(config2->data)));

    jewels::FactoryResult<std::reference_wrapper<TestConfig1>> value1;
    REQUIRE(jewels::ok(config.get_config<0>(jewels::Out{value1})));
    REQUIRE(*config1->data == *value1);
    std::shared_ptr<TestConfig1> handle1;
    REQUIRE(jewels::ok(config.get_config_handle<0>(jewels::Out{handle1})));
    REQUIRE(*config1->data == *handle1);
    jewels::FactoryResult<std::reference_wrapper<TestConfig2>> value2;
    REQUIRE(jewels::ok(config.get_config<1>(jewels::Out{value2})));
    REQUIRE(*config2->data == *value2);
    std::shared_ptr<TestConfig2> handle2;
    REQUIRE(jewels::ok(config.get_config_handle<1>(jewels::Out{handle2})));
    REQUIRE(*config2->data == *handle2);
  }
}

using ZeroConfigsPolicyFixture = CogConfigsFixture<>;

TEST_CASE_METHOD(ZeroConfigsPolicyFixture, "zero configs", "[cog_configs]")
{
  REQUIRE(config.validate());

  auto configs = config.make_configs(); // NOLINT(clang-analyzer-deadcode.DeadStores). Used for the decltype
  REQUIRE(0 == std::tuple_size<decltype(configs)>());
}

} // namespace
} // namespace clockwork
