// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/process_description.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "clockwork/pinion/tests/support/tmp_shm_namespace.hh"
#include "clockwork/scaffolding/abstract_casing.hh"
#include "clockwork/scaffolding/memory.hh"
#include "clockwork/scaffolding/state.hh"
#include "clockwork/scaffolding/tests/support/mock_casing.hh"
#include "clockwork/scaffolding/tests/support/runtime_tools.hh"
#include "clockwork/tags.hh"
#include "jewels/container/compare.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/monitor_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>
#include <trompeloeil/catch2.hpp> // IWYU pragma: keep (registers catch2 as the error handler)
#include <trompeloeil/matcher/any.hpp>
#include <trompeloeil/mock.hpp>
#include <xxh3.h>

#include <functional>
#include <memory>
#include <memory_resource>
#include <span>
#include <vector>

namespace clockwork::scaffolding
{
namespace
{

TEST_CASE("setup_states")
{
  using RetT = jewels::expected<void, AbstractCasing::Error>;

  const pinion::support::TmpShmNamespace tmp_namespace;
  auto channel_factory = tmp_namespace.make_factory();

  const auto state_1_id = jewels::Uuid<common::StateInstanceId>::random_uuid();
  const auto state_2_id = jewels::Uuid<common::StateInstanceId>::random_uuid();
  const auto state_3_id = jewels::Uuid<common::StateInstanceId>::random_uuid();
  const auto state_4_id = jewels::Uuid<common::StateInstanceId>::random_uuid();
  const auto state_1_schema = jewels::Uuid<RepresentationTag>::random_uuid();
  const auto state_2_schema = jewels::Uuid<RepresentationTag>::random_uuid();
  const auto state_3_schema = jewels::Uuid<RepresentationTag>::random_uuid();
  const auto state_4_schema = jewels::Uuid<RepresentationTag>::random_uuid();
  const auto state_1_memres = jewels::Uuid<common::MemoryResourceId>::random_uuid();
  const auto state_2_memres = jewels::Uuid<common::MemoryResourceId>::random_uuid();
  const auto state_4_memres = jewels::Uuid<common::MemoryResourceId>::random_uuid();

  const jewels::memory::MemoryResource memres_sys{std::pmr::get_default_resource()};

  MemResMap memres_map;
  memres_map[state_1_memres] = std::make_shared<jewels::memory::MonitorResource>();
  memres_map[state_2_memres] = std::make_shared<jewels::memory::MonitorResource>();
  memres_map[state_4_memres] = std::make_shared<jewels::memory::MonitorResource>();
  const jewels::memory::MemoryResource state_1_memres_v{memres_map[state_1_memres].get()};
  const jewels::memory::MemoryResource state_2_memres_v{memres_map[state_2_memres].get()};
  const jewels::memory::MemoryResource state_4_memres_v{memres_map[state_4_memres].get()};

  std::vector<common::StateInstanceDescriptionTap> configs;
  configs.emplace_back();
  configs.back().get_mutable_representation_id() = state_1_schema;
  configs.back().get_mutable_state_instance_id() = state_1_id;
  configs.back().get_underlying_instance_path_name().set_truncate("state_2");
  configs.back().reset_maybe_buffer_layout();
  configs.back().set_maybe_memory_resource(state_1_memres);
  configs.emplace_back();
  configs.back().get_mutable_representation_id() = state_2_schema;
  configs.back().get_mutable_state_instance_id() = state_2_id;
  configs.back().get_underlying_instance_path_name().set_truncate("state_2");
  configs.back().reset_maybe_buffer_layout();
  configs.back().set_maybe_memory_resource(state_2_memres);
  configs.emplace_back();
  configs.back().get_mutable_representation_id() = state_3_schema;
  configs.back().get_mutable_state_instance_id() = state_3_id;
  configs.back().get_underlying_instance_path_name().set_truncate("state_3");
  configs.back().set_maybe_buffer_layout({{.num_slots = 2, .message_size = 10}});
  configs.back().reset_maybe_memory_resource();
  configs.emplace_back();
  configs.back().get_mutable_representation_id() = state_4_schema;
  configs.back().get_mutable_state_instance_id() = state_4_id;
  configs.back().get_underlying_instance_path_name().set_truncate("state_4");
  configs.back().set_maybe_buffer_layout({{.num_slots = 3, .message_size = 11}});
  configs.back().set_maybe_memory_resource(state_4_memres);

  MockCasing casing;

  SECTION("okay")
  {
    REQUIRE_CALL(casing, try_instantiate_state(state_1_id, state_1_schema, state_1_memres_v)).RETURN(RetT{});
    REQUIRE_CALL(casing, try_instantiate_state(state_2_id, state_2_schema, state_2_memres_v)).RETURN(RetT{});
    REQUIRE_CALL(casing, try_instantiate_state(state_3_id, state_3_schema, ANY(pinion::PublisherHandle)))
      .RETURN(RetT{});
    REQUIRE_CALL(
      casing, try_instantiate_state(state_4_id, state_4_schema, ANY(pinion::PublisherHandle), state_4_memres_v))
      .RETURN(RetT{});
    {
      auto state_handles = setup_states(configs, memres_sys, memres_map, channel_factory, casing);
      CHECK(state_handles);
      CHECK(!configs.at(0).has_maybe_buffer_layout());
      CHECK(!configs.at(1).has_maybe_buffer_layout());
      CHECK(testing::make_snooper(channel_factory, state_3_id, configs.at(2).value_maybe_buffer_layout()));
      CHECK(testing::make_snooper(channel_factory, state_4_id, configs.at(3).value_maybe_buffer_layout()));
    }
    CHECK(!testing::make_snooper(channel_factory, state_3_id, configs.at(2).value_maybe_buffer_layout()));
    CHECK(!testing::make_snooper(channel_factory, state_4_id, configs.at(3).value_maybe_buffer_layout()));
  }
  SECTION("missing memres")
  {
    memres_map.erase(state_2_memres);
    REQUIRE_CALL(casing, try_instantiate_state(state_1_id, state_1_schema, state_1_memres_v)).RETURN(RetT{});
    CHECK(!setup_states(configs, memres_sys, memres_map, channel_factory, casing));
  }
  SECTION("casing error")
  {
    REQUIRE_CALL(casing, try_instantiate_state(state_1_id, state_1_schema, state_1_memres_v)).RETURN(RetT{});
    REQUIRE_CALL(casing, try_instantiate_state(state_2_id, state_2_schema, state_2_memres_v))
      .RETURN(RetT{jewels::unexpected(AbstractCasing::Error::invalid_instance_uuid)});
    CHECK(!setup_states(configs, memres_sys, memres_map, channel_factory, casing));
  }
}

TEST_CASE("connect_states")
{
  using RetT = jewels::expected<void, AbstractCasing::Error>;

  const jewels::memory::MemoryResource memres_sys(std::pmr::new_delete_resource());

  const auto state_1_id = jewels::Uuid<common::StateInstanceId>::random_uuid();
  const auto state_2_id = jewels::Uuid<common::StateInstanceId>::random_uuid();
  const auto state_3_id = jewels::Uuid<common::StateInstanceId>::random_uuid();
  const auto endpoint_1_id = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  const auto endpoint_2_id = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  const auto endpoint_3a_id = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  const auto endpoint_3b_id = jewels::Uuid<common::EndpointInstanceId>::random_uuid();

  std::vector<common::StateConnectionTap> configs;
  configs.emplace_back();
  configs.back().get_mutable_state_id() = state_1_id;
  configs.back().get_mutable_endpoint_id() = endpoint_1_id;
  configs.emplace_back();
  configs.back().get_mutable_state_id() = state_2_id;
  configs.back().get_mutable_endpoint_id() = endpoint_2_id;
  configs.emplace_back();
  configs.back().get_mutable_state_id() = state_3_id;
  configs.back().get_mutable_endpoint_id() = endpoint_3a_id;
  configs.emplace_back();
  configs.back().get_mutable_state_id() = state_3_id;
  configs.back().get_mutable_endpoint_id() = endpoint_3b_id;

  MockCasing casing;

  SECTION("okay")
  {
    REQUIRE_CALL(casing, try_connect_state(endpoint_1_id, state_1_id, false)).RETURN(RetT{});
    REQUIRE_CALL(casing, try_connect_state(endpoint_2_id, state_2_id, false)).RETURN(RetT{});
    REQUIRE_CALL(casing, try_connect_state(endpoint_3a_id, state_3_id, true)).RETURN(RetT{});
    REQUIRE_CALL(casing, try_connect_state(endpoint_3b_id, state_3_id, true)).RETURN(RetT{});
    CHECK(connect_states(configs, memres_sys, casing));
  }
  SECTION("casing error")
  {
    REQUIRE_CALL(casing, try_connect_state(endpoint_1_id, state_1_id, false)).RETURN(RetT{});
    REQUIRE_CALL(casing, try_connect_state(endpoint_2_id, state_2_id, false)).RETURN(RetT{});
    REQUIRE_CALL(casing, try_connect_state(endpoint_3a_id, state_3_id, true))
      .RETURN(RetT{jewels::unexpected(AbstractCasing::Error::invalid_instance_uuid)});
    CHECK(!connect_states(configs, memres_sys, casing));
  }
}

} // namespace
} // namespace clockwork::scaffolding
