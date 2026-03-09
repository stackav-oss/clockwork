// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/scaffolding/abstract_casing.hh"
#include "clockwork/scaffolding/memory.hh"
#include "clockwork/scaffolding/tests/support/mock_casing.hh"
#include "jewels/container/compare.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/monitor_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>
#include <trompeloeil/catch2.hpp> // IWYU pragma: keep (registers catch2 as the error handler)
#include <trompeloeil/mock.hpp>
#include <xxh3.h>

#include <functional>
#include <memory>
#include <memory_resource>
#include <span>
#include <unordered_map>
#include <vector>

namespace clockwork::scaffolding
{
namespace
{

TEST_CASE("memory")
{
  jewels::memory::MonitorResource memory;
  const jewels::memory::MemoryResource resource(memory);
  std::vector<Tappy<common::MemoryResource<>>> configs;

  SECTION("empty")
  {
    auto map = setup_memory_resources(configs, resource, resource);
    CHECK(map.has_value());
    CHECK(map->empty());
    CHECK(memory.used() == 0);
  }

  SECTION("default")
  {
    configs.emplace_back();
    configs.back().get_mutable_resource_type() = common::MemoryResourceType::new_delete;
    configs.back().get_mutable_resource_max_size() = 100;
    auto map = setup_memory_resources(configs, resource, resource);
    CHECK(map.has_value());
    CHECK(map->size() == configs.size());
    CHECK(memory.used() > 0);
    auto* cfg_res = dynamic_cast<jewels::memory::MonitorResource*>(map->at(configs[0].get_memory_resource_id()).get());
    CHECK(cfg_res != nullptr);
    CHECK(cfg_res->used() == 0);
  }
}

TEST_CASE("connect_memory_resources")
{
  using RetT = jewels::expected<void, AbstractCasing::Error>;

  const auto memory_1_id = jewels::Uuid<common::MemoryResourceId>::random_uuid();
  const auto memory_2_id = jewels::Uuid<common::MemoryResourceId>::random_uuid();
  const auto memory_3_id = jewels::Uuid<common::MemoryResourceId>::random_uuid();
  const auto endpoint_1_id = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  const auto endpoint_2_id = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  const auto endpoint_3a_id = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  const auto endpoint_3b_id = jewels::Uuid<common::EndpointInstanceId>::random_uuid();

  MemResMap memres_map;
  memres_map[memory_1_id] = std::make_shared<jewels::memory::MonitorResource>();
  memres_map[memory_2_id] = std::make_shared<jewels::memory::MonitorResource>();
  memres_map[memory_3_id] = std::make_shared<jewels::memory::MonitorResource>();
  const jewels::memory::MemoryResource memory_1_v{memres_map[memory_1_id].get()};
  const jewels::memory::MemoryResource memory_2_v{memres_map[memory_2_id].get()};
  const jewels::memory::MemoryResource memory_3_v{memres_map[memory_3_id].get()};

  std::vector<Tappy<common::MemoryResourceConnection>> configs;
  configs.emplace_back();
  configs.back().set_memory_resource_id(memory_1_id);
  configs.back().set_endpoint_id(endpoint_1_id);
  configs.emplace_back();
  configs.back().set_memory_resource_id(memory_2_id);
  configs.back().set_endpoint_id(endpoint_2_id);
  configs.emplace_back();
  configs.back().set_memory_resource_id(memory_3_id);
  configs.back().set_endpoint_id(endpoint_3a_id);
  configs.emplace_back();
  configs.back().set_memory_resource_id(memory_3_id);
  configs.back().set_endpoint_id(endpoint_3b_id);

  MockCasing casing;

  SECTION("okay")
  {
    REQUIRE_CALL(casing, try_connect_memory_resource(endpoint_1_id, memory_1_v)).RETURN(RetT{});
    REQUIRE_CALL(casing, try_connect_memory_resource(endpoint_2_id, memory_2_v)).RETURN(RetT{});
    REQUIRE_CALL(casing, try_connect_memory_resource(endpoint_3a_id, memory_3_v)).RETURN(RetT{});
    REQUIRE_CALL(casing, try_connect_memory_resource(endpoint_3b_id, memory_3_v)).RETURN(RetT{});
    CHECK(connect_memory_resources(configs, memres_map, casing));
  }
  SECTION("casing error")
  {
    REQUIRE_CALL(casing, try_connect_memory_resource(endpoint_1_id, memory_1_v)).RETURN(RetT{});
    REQUIRE_CALL(casing, try_connect_memory_resource(endpoint_2_id, memory_2_v)).RETURN(RetT{});
    REQUIRE_CALL(casing, try_connect_memory_resource(endpoint_3a_id, memory_3_v))
      .RETURN(RetT{jewels::unexpected(AbstractCasing::Error::invalid_instance_uuid)});
    CHECK(!connect_memory_resources(configs, memres_map, casing));
  }
}

} // namespace
} // namespace clockwork::scaffolding
