// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "clockwork/pinion/tests/support/tmp_shm_namespace.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/scaffolding/abstract_casing.hh"
#include "clockwork/scaffolding/data_source_loader.hh"
#include "clockwork/scaffolding/memory.hh"
#include "clockwork/scaffolding/state.hh"
#include "clockwork/scaffolding/tests/support/mock_casing.hh"
#include "clockwork/scaffolding/tests/support/runtime_tools.hh"
#include "clockwork/tags.hh"
#include "jewels/container/compare.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/filesystem/file.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/monitor_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>
#include <trompeloeil/catch2.hpp> // IWYU pragma: keep (registers catch2 as the error handler)
#include <trompeloeil/matcher/any.hpp>
#include <trompeloeil/mock.hpp>
#include <xxh3.h>

#include <algorithm>
#include <cstddef>
#include <fcntl.h>
#include <functional>
#include <memory>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <unistd.h>
#include <utility>
#include <vector>

namespace clockwork::scaffolding
{
namespace
{

void write(const jewels::filesystem::Directory& dir, std::string_view name, std::span<const std::byte> data)
{
  const jewels::filesystem::File file{dir.descriptor(), name, O_CREAT | O_WRONLY};
  CHECK(::write(file.descriptor(), data.data(), data.size()) == static_cast<ssize_t>(data.size()));
}

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

  std::vector<Tappy<common::StateInstanceDescription<>>> configs;
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
  configs.back().set_maybe_buffer_layout({{.num_slots = 2, .message_size = 10, .is_published_once = false}});
  configs.back().reset_maybe_memory_resource();
  configs.emplace_back();
  configs.back().get_mutable_representation_id() = state_4_schema;
  configs.back().get_mutable_state_instance_id() = state_4_id;
  configs.back().get_underlying_instance_path_name().set_truncate("state_4");
  configs.back().set_maybe_buffer_layout({{.num_slots = 3, .message_size = 11, .is_published_once = false}});
  configs.back().set_maybe_memory_resource(state_4_memres);

  std::vector<Tappy<common::DataSource<>>> data_sources;
  FirstMessageCache first_message_cache(memres_sys);

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
      auto state_handles = setup_states(
        configs, memres_sys, memres_map, channel_factory, casing, std::span{data_sources}, first_message_cache);
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
    CHECK(!setup_states(
      configs, memres_sys, memres_map, channel_factory, casing, std::span{data_sources}, first_message_cache));
  }
  SECTION("casing error")
  {
    REQUIRE_CALL(casing, try_instantiate_state(state_1_id, state_1_schema, state_1_memres_v)).RETURN(RetT{});
    REQUIRE_CALL(casing, try_instantiate_state(state_2_id, state_2_schema, state_2_memres_v))
      .RETURN(RetT{jewels::unexpected(AbstractCasing::Error::invalid_instance_uuid)});
    CHECK(!setup_states(
      configs, memres_sys, memres_map, channel_factory, casing, std::span{data_sources}, first_message_cache));
  }
  SECTION("load data fails")
  {
    std::vector<Tappy<common::StateInstanceDescription<>>> test_configs = configs;
    test_configs[0].set_init_data_source(42);
    CHECK(!setup_states(
      test_configs, memres_sys, memres_map, channel_factory, casing, std::span{data_sources}, first_message_cache));
  }
  SECTION("init data but no buffer")
  {
    std::vector<Tappy<common::StateInstanceDescription<>>> test_configs;
    test_configs.emplace_back();
    test_configs.back().get_mutable_representation_id() = state_1_schema;
    test_configs.back().get_mutable_state_instance_id() = state_1_id;
    test_configs.back().get_underlying_instance_path_name().set_truncate("state_1");
    test_configs.back().reset_maybe_buffer_layout();
    test_configs.back().reset_maybe_memory_resource();
    test_configs.back().set_init_data_source(0);
    std::vector<Tappy<common::DataSource<>>> test_data_sources = {TapInit<Tachyon<common::DataSource<4096>>>{
      .representation_id = state_1_schema,
      .data_source_type = common::DataSourceType::file,
      .source_path_or_name = jewels::tap::VarString<4096>{"/dev/null"},
      .fallback_source = common::no_fallback_data_source_sentinel}};
    CHECK(!setup_states(
      test_configs,
      memres_sys,
      memres_map,
      channel_factory,
      casing,
      std::span{test_data_sources},
      first_message_cache));
  }

  SECTION("with init data, matching representation")
  {
    const auto state_5_id = jewels::Uuid<common::StateInstanceId>::random_uuid();
    const auto state_5_schema = jewels::Uuid<RepresentationTag>::random_uuid();
    std::vector<Tappy<common::StateInstanceDescription<>>> test_configs;
    test_configs.emplace_back();
    test_configs.back().get_mutable_representation_id() = state_5_schema;
    test_configs.back().get_mutable_state_instance_id() = state_5_id;
    test_configs.back().get_underlying_instance_path_name().set_truncate("state_5");
    test_configs.back().set_maybe_buffer_layout({{.num_slots = 1, .message_size = 5, .is_published_once = false}});
    test_configs.back().reset_maybe_memory_resource();
    test_configs.back().set_init_data_source(0);

    std::vector<std::byte> init_data{std::byte{1}, std::byte{2}, std::byte{3}};
    const std::string test_filename = "data1.dat";
    const jewels::testing::TmpDirectoryGuard tmpdir_obj;
    const jewels::filesystem::Directory tmpdir{tmpdir_obj.get_path().c_str()};
    write(tmpdir, test_filename, std::as_bytes(std::span(init_data)));

    std::vector<Tappy<common::DataSource<>>> test_data_sources = {TapInit<Tachyon<common::DataSource<4096>>>{
      .representation_id = state_5_schema,
      .data_source_type = common::DataSourceType::file,
      .source_path_or_name = jewels::tap::VarString<4096>{""},
      .fallback_source = common::no_fallback_data_source_sentinel}};
    CHECK(
      test_data_sources[0].get_underlying_source_path_or_name().try_set(
        (tmpdir_obj.get_path() / test_filename).string()));

    REQUIRE_CALL(
      casing,
      try_instantiate_state(state_5_id, state_5_schema, ANY(pinion::PublisherHandle), ANY(std::span<const std::byte>)))
      .RETURN(AbstractCasing::Outcome{AbstractCasing::OutcomeEnum::success});
    auto state_handles = setup_states(
      test_configs, memres_sys, memres_map, channel_factory, casing, std::span{test_data_sources}, first_message_cache);
    CHECK(state_handles);
  }
  SECTION("with init data, deserialization needed")
  {
    const auto state_6_id = jewels::Uuid<common::StateInstanceId>::random_uuid();
    const auto state_6_schema = jewels::Uuid<RepresentationTag>::random_uuid();
    const auto data_rep_id = jewels::Uuid<RepresentationTag>::random_uuid();
    std::vector<Tappy<common::StateInstanceDescription<>>> test_configs;
    test_configs.emplace_back();
    test_configs.back().get_mutable_representation_id() = state_6_schema;
    test_configs.back().get_mutable_state_instance_id() = state_6_id;
    test_configs.back().get_underlying_instance_path_name().set_truncate("state_6");
    test_configs.back().set_maybe_buffer_layout({{.num_slots = 1, .message_size = 5, .is_published_once = false}});
    test_configs.back().reset_maybe_memory_resource();
    test_configs.back().set_init_data_source(0);

    std::pmr::vector<std::byte> init_data{memres_sys};
    init_data.assign({std::byte{4}, std::byte{5}, std::byte{6}});
    std::pmr::vector<std::byte> deserialized_data{memres_sys};
    deserialized_data.assign({std::byte{7}, std::byte{8}, std::byte{9}});
    FirstMessageCache test_first_message_cache(memres_sys);
    test_first_message_cache["inline_data"] = init_data;
    std::vector<Tappy<common::DataSource<>>> test_data_sources = {TapInit<Tachyon<common::DataSource<4096>>>{
      .representation_id = data_rep_id,
      .data_source_type = common::DataSourceType::log_first_message,
      .source_path_or_name = jewels::tap::VarString<4096>{"inline_data"},
      .fallback_source = common::no_fallback_data_source_sentinel}};

    REQUIRE_CALL(casing, try_deserialize_data(data_rep_id, init_data, ANY(std::span<std::byte>)))
      .SIDE_EFFECT(
        std::copy(deserialized_data.begin(), deserialized_data.end(), std::get<2>(trompeloeil_x).get().begin()))
      .RETURN(AbstractCasing::Outcome{AbstractCasing::OutcomeEnum::success});
    REQUIRE_CALL(
      casing,
      try_instantiate_state(state_6_id, state_6_schema, ANY(pinion::PublisherHandle), ANY(std::span<const std::byte>)))
      .RETURN(AbstractCasing::Outcome{AbstractCasing::OutcomeEnum::success});
    auto state_handles = setup_states(
      test_configs,
      memres_sys,
      memres_map,
      channel_factory,
      casing,
      std::span{test_data_sources},
      test_first_message_cache);
    CHECK(state_handles);
  }
  SECTION("deserialization fails")
  {
    const auto state_7_id = jewels::Uuid<common::StateInstanceId>::random_uuid();
    const auto state_7_schema = jewels::Uuid<RepresentationTag>::random_uuid();
    const auto data_rep_id = jewels::Uuid<RepresentationTag>::random_uuid();
    std::vector<Tappy<common::StateInstanceDescription<>>> test_configs;
    test_configs.emplace_back();
    test_configs.back().get_mutable_representation_id() = state_7_schema;
    test_configs.back().get_mutable_state_instance_id() = state_7_id;
    test_configs.back().get_underlying_instance_path_name().set_truncate("state_7");
    test_configs.back().set_maybe_buffer_layout({{.num_slots = 1, .message_size = 5, .is_published_once = false}});
    test_configs.back().reset_maybe_memory_resource();
    test_configs.back().set_init_data_source(0);

    std::pmr::vector<std::byte> init_data{memres_sys};
    init_data.assign({std::byte{10}, std::byte{11}});
    FirstMessageCache test_first_message_cache(memres_sys);
    test_first_message_cache["inline_data_fail"] = init_data;
    std::vector<Tappy<common::DataSource<>>> test_data_sources = {TapInit<Tachyon<common::DataSource<4096>>>{
      .representation_id = data_rep_id,
      .data_source_type = common::DataSourceType::log_first_message,
      .source_path_or_name = jewels::tap::VarString<4096>{"inline_data_fail"},
      .fallback_source = common::no_fallback_data_source_sentinel}};

    REQUIRE_CALL(casing, try_deserialize_data(data_rep_id, init_data, ANY(std::span<std::byte>)))
      .RETURN(AbstractCasing::Outcome{AbstractCasing::OutcomeEnum::invalid_instance_uuid});
    CHECK(!setup_states(
      test_configs,
      memres_sys,
      memres_map,
      channel_factory,
      casing,
      std::span{test_data_sources},
      test_first_message_cache));
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

  std::vector<Tappy<common::StateConnection>> configs;
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
