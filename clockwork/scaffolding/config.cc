// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/scaffolding/config.hh"

#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/scaffolding/abstract_casing.hh"
#include "clockwork/scaffolding/data_source_loader.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <string_view>

namespace clockwork::scaffolding
{

jewels::expected<void, jewels::MonoError> setup_configs(
  std::span<const Tappy<common::ConfigInstanceDescription<>>> descs,
  std::span<const Tappy<common::DataSource<>>> data_sources,
  jewels::memory::MemoryResource memres_sys,
  jewels::memory::MemoryResource memres_cfg,
  const FirstMessageCache& first_message_cache,
  AbstractCasing& casing)
{
  for (const auto& desc : descs)
  {
    const auto data_source_idx = desc.get_init_data_source();

    DataSourceLoadResult load_result{memres_sys};
    if (jewels::fails(load_data_from_source(
          jewels::Out{load_result},
          data_source_idx,
          data_sources,
          memres_sys,
          first_message_cache,
          desc.get_instance_path_name())))
    {
      jewels::log_cerr_error("error loading data for config '{}'", desc.get_instance_path_name());
      return jewels::unexpected(jewels::MonoError());
    }

    if (load_result.should_default_construct)
    {
      jewels::log_cerr_error("config '{}': default construction is not yet implemented", desc.get_instance_path_name());
      return jewels::unexpected(jewels::MonoError());
    }

    if (auto result = casing.try_instantiate_config(
          desc.get_config_instance_id(), load_result.representation_id, load_result.data, memres_cfg);
        !result)
    {
      jewels::log_cerr_error(
        "error creating config '{}': {}\nTypically this is because you have not declared the config representation in "
        "the casing `representation Protobuf<>`. Note, you can also just delcare your box directly in the casing and "
        "it will automatically declare everything in your box.",
        desc.get_instance_path_name(),
        result.error());
      return jewels::unexpected(jewels::MonoError());
    }
  }
  return {};
}

[[nodiscard]] jewels::expected<void, jewels::MonoError>
connect_configs(std::span<const Tappy<common::ConfigConnection>> connections, AbstractCasing& casing)
{
  for (const auto& connection : connections)
  {
    auto result = casing.try_connect_config(connection.get_endpoint_id(), connection.get_config_id());
    if (!result)
    {
      jewels::log_cerr_error(
        "try_connect_config {} -> {} failed: {}",
        connection.get_config_id(),
        connection.get_endpoint_id(),
        result.error());
      return jewels::unexpected(jewels::MonoError());
    }
  }
  return {};
}

} // namespace clockwork::scaffolding
