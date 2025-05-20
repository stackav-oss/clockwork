// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/abstract_cog.hh"
#include "clockwork/common/abstract_cog_queue.hh"
#include "clockwork/common/process_description.hh"
#include "clockwork/scaffolding/abstract_casing.hh"
#include "clockwork/scaffolding/cog.hh"
#include "clockwork/scaffolding/tests/support/mock_casing.hh"
#include "jewels/container/compare.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>
#include <trompeloeil/catch2.hpp> // IWYU pragma: keep (registers catch2 as the error handler)
#include <trompeloeil/mock.hpp>

#include <functional>
#include <memory>
#include <memory_resource>
#include <span>
#include <vector>

namespace clockwork::scaffolding
{
namespace
{

TEST_CASE("setup_cogs")
{
  using RetT = jewels::expected<std::shared_ptr<AbstractCog>, AbstractCasing::Error>;

  const jewels::memory::MemoryResource memres_sys(std::pmr::new_delete_resource());
  const jewels::memory::MemoryResource memres_exec(std::pmr::new_delete_resource());

  const std::shared_ptr<AbstractCogQueue> queue;

  std::vector<common::CogInstanceDescriptionTap> configs;
  configs.emplace_back();
  configs.back().set_cog_instance_id(jewels::Uuid<common::CogInstanceId>::random_uuid());
  configs.back().get_underlying_instance_path_name().set_truncate("cog1");
  configs.emplace_back();
  configs.back().set_cog_instance_id(jewels::Uuid<common::CogInstanceId>::random_uuid());
  configs.back().get_underlying_instance_path_name().set_truncate("cog2");

  MockCasing casing;

  SECTION("okay")
  {
    REQUIRE_CALL(casing, try_instantiate_cog(configs[0], queue, memres_exec)).RETURN(RetT{});
    REQUIRE_CALL(casing, try_instantiate_cog(configs[1], queue, memres_exec)).RETURN(RetT{});
    auto result = setup_cogs(configs, memres_sys, memres_exec, queue, casing);
    REQUIRE(result);
  }
  SECTION("casing error")
  {
    REQUIRE_CALL(casing, try_instantiate_cog(configs[0], queue, memres_exec))
      .RETURN(RetT{jewels::unexpected(AbstractCasing::Error::invalid_class_uuid)});
    auto result = setup_cogs(configs, memres_sys, memres_exec, queue, casing);
    REQUIRE(!result);
  }
}

} // namespace
} // namespace clockwork::scaffolding
