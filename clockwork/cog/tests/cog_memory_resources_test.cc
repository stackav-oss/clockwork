// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/cog_memory_resources.hh"
#include "clockwork/common/process_description.hh"
#include "jewels/container/compare.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>
#include <gsl/util>

#include <memory>
#include <memory_resource>
#include <string_view>
#include <tuple>

namespace clockwork
{
namespace
{

template <typename... Policies>
struct CogMemoryResourcesFixture // NOLINT(clang-analyzer-optin.performance.Padding) Test code so performance is not a
                                 // concern.
{
  static constexpr auto policy_count = sizeof...(Policies);

  CogMemoryResources<Policies...> memory_resource;
};

struct MemoryResourcePolicy1
{
  using MemoryResourceType = jewels::memory::MemoryResource;
  static constexpr auto endpoint_id =
    jewels::Uuid<common::EndpointClassId>::from_string("b6e2b628-62ba-4c73-b07e-b2ce77a742b4").value();
  static constexpr std::string_view name = "MemoryResourcePolicy1";
};

struct MemoryResourcePolicy2
{
  using MemoryResourceType = jewels::memory::MemoryResource;
  static constexpr auto endpoint_id =
    jewels::Uuid<common::EndpointClassId>::from_string("789e340c-556f-4cf0-a3b9-73632ba75a7c").value();
  static constexpr std::string_view name = "MemoryResourcePolicy2";
};

using MemoryResourcePolicyFixture = CogMemoryResourcesFixture<MemoryResourcePolicy1, MemoryResourcePolicy2>;

TEST_CASE_METHOD(MemoryResourcePolicyFixture, "basic operation", "[cog_memory_resources]")
{
  REQUIRE_FALSE(memory_resource.validate());

  // Set memory_resources

  auto memory_resource1 = std::make_shared<jewels::memory::MemoryResource>(std::pmr::new_delete_resource());
  auto memory_resource2 = std::make_shared<jewels::memory::MemoryResource>(std::pmr::new_delete_resource());

  SECTION("set_handle unknown id")
  {
    constexpr auto unknown_id =
      jewels::Uuid<common::EndpointClassId>::from_string("b5e2c9a9-e351-4877-b5cc-77e76a7ebe63").value();
    REQUIRE_FALSE(memory_resource.set_handle(unknown_id, memory_resource1));
  }

  REQUIRE_FALSE(memory_resource.validate());
  REQUIRE(memory_resource.set_handle(MemoryResourcePolicy1::endpoint_id, *memory_resource1));
  REQUIRE_FALSE(memory_resource.validate());
  REQUIRE(memory_resource.set_handle(MemoryResourcePolicy2::endpoint_id, *memory_resource2));
  REQUIRE(memory_resource.validate());

  auto memory_resources = memory_resource.make_memory_resources();
  REQUIRE(2 == std::tuple_size<decltype(memory_resources)>());

  auto actual1 = std::get<0>(memory_resources);
  REQUIRE(*memory_resource1 == actual1);
  auto actual2 = std::get<1>(memory_resources);
  REQUIRE(*memory_resource2 == actual2);
}

using ZeroMemoryResourcesPolicyFixture = CogMemoryResourcesFixture<>;

TEST_CASE_METHOD(ZeroMemoryResourcesPolicyFixture, "zero memory_resources", "[cog_memory_resources]")
{
  REQUIRE(memory_resource.validate());

  auto memory_resources = memory_resource.make_memory_resources(); // NOLINT(clang-analyzer-deadcode.DeadStores)
  REQUIRE(0 == std::tuple_size<decltype(memory_resources)>());
}

} // namespace
} // namespace clockwork
