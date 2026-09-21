// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/scaffolding/validate_casing.hh"

#include "clockwork/cog/factory.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/scaffolding/abstract_casing.hh"
#include "jewels/container/compare.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/uuid/uuid.hh"

#include <cstdlib>
#include <span>
#include <string_view>

namespace clockwork::scaffolding
{

int validate_casing(const Tappy<common::ProcessDescription<>>& desc, const AbstractCasing& casing)
{
  std::size_t missing = 0;

  // Cog classes -> CogFactory linked list
  for (const auto& cog_inst : desc.get_cog_instances())
  {
    const auto& uuid = cog_inst.get_cog_class_id();
    if (CogFactory::find(uuid) == nullptr)
    {
      jewels::log_cerr_error(
        "missing cog class '{}' for instance '{}' (instance_id={})",
        uuid,
        cog_inst.get_instance_path_name(),
        cog_inst.get_cog_instance_id());
      ++missing;
    }
  }

  // State representation UUIDs -> CogStateFactory (global) OR the casing's
  // Schemas tuple (for hybrid / cxx state).  Either is sufficient.
  for (const auto& state_inst : desc.get_state_graph().get_state_instances())
  {
    const auto& uuid = state_inst.get_representation_id();
    if (CogStateFactory::find(uuid) == nullptr && !casing.has_schema_representation(uuid))
    {
      jewels::log_cerr_error(
        "missing state representation '{}' for state instance '{}' ('{}')",
        uuid,
        state_inst.get_state_instance_id(),
        state_inst.get_instance_path_name());
      ++missing;
    }
  }

  // Config / data-source representation UUIDs -> casing's Schemas tuple.
  for (std::size_t i = 0; i < desc.get_data_sources().size(); ++i)
  {
    const auto& data_source = desc.get_data_sources()[i];
    const auto& uuid = data_source.get_representation_id();
    if (!casing.has_schema_representation(uuid))
    {
      jewels::log_cerr_error(
        "missing schema representation '{}' for data source #{} (path='{}')",
        uuid,
        i,
        data_source.get_source_path_or_name());
      ++missing;
    }
  }

  // IO connection class UUIDs -> casing's IoConnections tuple.
  for (const auto& io_inst : desc.get_io_connections())
  {
    const auto& uuid = io_inst.get_class_id();
    if (!casing.has_io_connection_class(uuid))
    {
      jewels::log_cerr_error(
        "missing io connection class '{}' for io instance '{}' ('{}')",
        uuid,
        io_inst.get_instance_id(),
        io_inst.get_instance_path_name());
      ++missing;
    }
  }

  if (missing == 0)
  {
    jewels::log_cerr_info(
      "casing validation OK: {} cogs, {} states, {} data sources, {} io connections all linked",
      desc.get_cog_instances().size(),
      desc.get_state_graph().get_state_instances().size(),
      desc.get_data_sources().size(),
      desc.get_io_connections().size());
    return EXIT_SUCCESS;
  }

  jewels::log_cerr_error("casing validation FAILED: {} missing class(es)", missing);
  return EXIT_FAILURE;
}

} // namespace clockwork::scaffolding
