// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/include_common.hh"
#include "clockwork/scaffolding/tests/support/mock_casing.hh"
#include "clockwork/scaffolding/tests/support/test_cogs.hh"
#include "clockwork/scaffolding/validate_casing.hh"
#include "clockwork/tags.hh"
#include "jewels/container/tap/var_array.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/memory/aligned_storage.hh"

#include <catch2/catch_test_macros.hpp>
#include <trompeloeil/catch2.hpp> // IWYU pragma: keep
#include <trompeloeil/mock.hpp>

#include <cstdlib>
#include <memory>

namespace clockwork::scaffolding
{
namespace
{

using trompeloeil::_;

// Populate one entry of each UUID-bearing kind: cog instance, state instance,
// data source, io connection.  Callers configure the mock casing's responses
// to drive success vs. failure for each kind.
void populate_one_of_each(Tappy<common::ProcessDescription<>>& desc, jewels::Uuid<common::CogClassId> cog_class_id)
{
  auto& cog_inst = desc.get_underlying_cog_instances().emplace_back();
  cog_inst.set_cog_class_id(cog_class_id);
  cog_inst.set_cog_instance_id(jewels::Uuid<common::CogInstanceId>::random_uuid());
  cog_inst.get_underlying_instance_path_name().set_truncate("cog");

  auto& state_inst = desc.get_mutable_state_graph().get_underlying_state_instances().emplace_back();
  state_inst.set_representation_id(jewels::Uuid<RepresentationTag>::random_uuid());
  state_inst.set_state_instance_id(jewels::Uuid<common::StateInstanceId>::random_uuid());
  state_inst.get_underlying_instance_path_name().set_truncate("state");

  auto& data_source = desc.get_underlying_data_sources().emplace_back();
  data_source.set_representation_id(jewels::Uuid<RepresentationTag>::random_uuid());
  data_source.get_underlying_source_path_or_name().set_truncate("/path/to/config");

  auto& io_conn = desc.get_underlying_io_connections().emplace_back();
  io_conn.set_class_id(jewels::Uuid<common::IoConnectionClassId>::random_uuid());
  io_conn.set_instance_id(jewels::Uuid<common::IoConnectionInstanceId>::random_uuid());
  io_conn.get_underlying_instance_path_name().set_truncate("io_conn");
}

TEST_CASE("validate_casing: all known UUIDs (and empty PDF) succeed")
{
  // Empty PDF: no walk runs, no mock calls, exit success.
  {
    auto desc = std::make_unique<Tappy<common::ProcessDescription<>>>();
    MockCasing casing;
    REQUIRE(validate_casing(*desc, casing) == EXIT_SUCCESS);
  }

  // One of each kind, all resolvable: cog via global CogFactory (TestCog1),
  // state + data source via casing's Schemas tuple, io connection via
  // IoConnections tuple.  trompeloeil ALLOW_CALL accepts any number of calls.
  auto desc = std::make_unique<Tappy<common::ProcessDescription<>>>();
  populate_one_of_each(*desc, testing::TestCog1Factory::type_id);

  MockCasing casing;
  ALLOW_CALL(casing, has_schema_representation(_)).RETURN(true);
  ALLOW_CALL(casing, has_io_connection_class(_)).RETURN(true);
  REQUIRE(validate_casing(*desc, casing) == EXIT_SUCCESS);
}

TEST_CASE("validate_casing: every kind of missing UUID is reported, no early exit")
{
  auto desc = std::make_unique<Tappy<common::ProcessDescription<>>>();
  // Random cog class id (unknown to global CogFactory).
  populate_one_of_each(*desc, jewels::Uuid<common::CogClassId>::random_uuid());
  // Add a second unknown cog to confirm we keep walking after failures.
  auto& cog2 = desc->get_underlying_cog_instances().emplace_back();
  cog2.set_cog_class_id(jewels::Uuid<common::CogClassId>::random_uuid());
  cog2.set_cog_instance_id(jewels::Uuid<common::CogInstanceId>::random_uuid());
  cog2.get_underlying_instance_path_name().set_truncate("cog2");

  MockCasing casing;
  // Casing reports "not in tuple" for all schema and io queries.
  ALLOW_CALL(casing, has_schema_representation(_)).RETURN(false);
  ALLOW_CALL(casing, has_io_connection_class(_)).RETURN(false);
  REQUIRE(validate_casing(*desc, casing) == EXIT_FAILURE);
}

} // namespace
} // namespace clockwork::scaffolding
