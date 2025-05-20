// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/process_description.hh"
#include "clockwork/scaffolding/abstract_casing.hh"
#include "clockwork/scaffolding/config.hh"
#include "clockwork/scaffolding/tests/support/mock_casing.hh"
#include "clockwork/tags.hh"
#include "jewels/container/compare.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/filesystem/file.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/monitor_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>
#include <trompeloeil/catch2.hpp> // IWYU pragma: keep (registers catch2 as the error handler)
#include <trompeloeil/mock.hpp>

#include <cstddef>
#include <fcntl.h>
#include <filesystem>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <unistd.h>
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

TEST_CASE("setup_configs")
{
  using RetT = jewels::expected<void, AbstractCasing::Error>;

  constexpr std::string_view config1_str = "abc123";
  constexpr std::string_view config2_str = "aeiou";

  const auto config1_dat = std::as_bytes(std::span(config1_str));
  const auto config1_schema = jewels::Uuid<RepresentationTag>::random_uuid();
  const auto config1_instance = jewels::Uuid<common::ConfigInstanceId>::random_uuid();
  const auto config1_filename = jewels::Uuid<common::ConfigInstanceId>::random_uuid().to_string() + ".conf";
  const auto config2_dat = std::as_bytes(std::span(config2_str));
  const auto config2_schema = jewels::Uuid<RepresentationTag>::random_uuid();
  const auto config2_instance = jewels::Uuid<common::ConfigInstanceId>::random_uuid();
  const auto config2_filename = jewels::Uuid<common::ConfigInstanceId>::random_uuid().to_string() + ".conf";

  const jewels::testing::TmpDirectoryGuard tmpdir_obj;
  const jewels::filesystem::Directory tmpdir{tmpdir_obj.get_path().c_str()};

  jewels::memory::MonitorResource memory;
  const jewels::memory::MemoryResource sysres{memory};
  const jewels::memory::MemoryResource cfgres{std::pmr::new_delete_resource()};
  MockCasing casing;

  SECTION("empty")
  {
    std::vector<common::ConfigInstanceDescriptionTap> descs;
    auto result = setup_configs(descs, sysres, cfgres, casing);
    REQUIRE(result);
    CHECK(memory.peak() == 0);
  }

  SECTION("basic")
  {
    write(tmpdir, config1_filename, config1_dat);

    std::vector<common::ConfigInstanceDescriptionTap> descs;
    descs.emplace_back();
    descs.back().get_mutable_representation_id() = config1_schema;
    descs.back().get_mutable_config_instance_id() = config1_instance;
    CHECK(descs.back().get_underlying_config_file_path().try_set((tmpdir_obj.get_path() / config1_filename).string()));
    CHECK(descs.back().get_underlying_instance_path_name().try_set("node.config1"));

    SECTION("okay")
    {
      REQUIRE_CALL(casing, try_instantiate_config(config1_instance, config1_schema, config1_dat, cfgres))
        .RETURN(RetT{});
      auto result = setup_configs(descs, sysres, cfgres, casing);
      REQUIRE(result);
      CHECK(memory.peak() == config1_dat.size() + 1);
    }

    SECTION("parse error")
    {
      REQUIRE_CALL(casing, try_instantiate_config(config1_instance, config1_schema, config1_dat, cfgres))
        .RETURN(jewels::unexpected(AbstractCasing::Error::invalid_class_uuid));
      auto result = setup_configs(descs, sysres, cfgres, casing);
      REQUIRE(!result);
      CHECK(memory.peak() == config1_dat.size() + 1);
    }

    SECTION("two files")
    {
      REQUIRE_CALL(casing, try_instantiate_config(config1_instance, config1_schema, config1_dat, cfgres))
        .RETURN(RetT{});
      descs.emplace_back();
      descs.back().get_mutable_representation_id() = config2_schema;
      descs.back().get_mutable_config_instance_id() = config2_instance;
      CHECK(
        descs.back().get_underlying_config_file_path().try_set((tmpdir_obj.get_path() / config2_filename).string()));
      CHECK(descs.back().get_underlying_instance_path_name().try_set("node.config2"));

      SECTION("okay")
      {
        REQUIRE_CALL(casing, try_instantiate_config(config2_instance, config2_schema, config2_dat, cfgres))
          .RETURN(RetT{});
        write(tmpdir, config2_filename, config2_dat);
        auto result = setup_configs(descs, sysres, cfgres, casing);
        REQUIRE(result);
        CHECK(memory.peak() == config1_dat.size() + 1);
      }

      SECTION("file error")
      {
        auto result = setup_configs(descs, sysres, cfgres, casing);
        REQUIRE(!result);
        CHECK(memory.peak() == config1_dat.size() + 1);
      }
    }
  }
}

TEST_CASE("connect_configs")
{
  using RetT = jewels::expected<void, AbstractCasing::Error>;
  const auto config1 = jewels::Uuid<common::ConfigInstanceId>::random_uuid();
  const auto config2 = jewels::Uuid<common::ConfigInstanceId>::random_uuid();
  const auto endpoint1 = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  const auto endpoint2 = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  const auto endpoint3 = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  const auto endpoint4 = jewels::Uuid<common::EndpointInstanceId>::random_uuid();

  std::vector<common::ConfigConnectionTap> descs;
  descs.emplace_back();
  descs.back().get_mutable_endpoint_id() = endpoint1;
  descs.back().get_mutable_config_id() = config1;
  descs.emplace_back();
  descs.back().get_mutable_endpoint_id() = endpoint2;
  descs.back().get_mutable_config_id() = config1;
  descs.emplace_back();
  descs.back().get_mutable_endpoint_id() = endpoint3;
  descs.back().get_mutable_config_id() = config1;
  descs.emplace_back();
  descs.back().get_mutable_endpoint_id() = endpoint4;
  descs.back().get_mutable_config_id() = config2;

  MockCasing casing;

  SECTION("okay")
  {
    REQUIRE_CALL(casing, try_connect_config(endpoint1, config1)).RETURN(RetT{});
    REQUIRE_CALL(casing, try_connect_config(endpoint2, config1)).RETURN(RetT{});
    REQUIRE_CALL(casing, try_connect_config(endpoint3, config1)).RETURN(RetT{});
    REQUIRE_CALL(casing, try_connect_config(endpoint4, config2)).RETURN(RetT{});
    auto result = connect_configs(descs, casing);
    REQUIRE(result);
  }
  SECTION("parse error")
  {
    REQUIRE_CALL(casing, try_connect_config(endpoint1, config1)).RETURN(RetT{});
    REQUIRE_CALL(casing, try_connect_config(endpoint2, config1)).RETURN(RetT{});
    REQUIRE_CALL(casing, try_connect_config(endpoint3, config1))
      .RETURN(jewels::unexpected(AbstractCasing::Error::invalid_instance_uuid));
    auto result = connect_configs(descs, casing);
    REQUIRE(!result);
  }
}

} // namespace
} // namespace clockwork::scaffolding
