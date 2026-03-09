// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/scaffolding/abstract_casing.hh"
#include "clockwork/scaffolding/data_source_loader.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"

#include <span>

namespace clockwork::scaffolding
{

///
/// Read in the files and invoke casing->try_instantiate_config for each of the specified config
/// @param descs list of config instance descriptions
/// @param data_sources list of data sources that configs reference
/// @param memres_sys memory resource used for system level bookkeeping (i.e. allocating the return array)
/// @param memres_cfg memory resource used for storing config file data
/// @param first_message_cache cache of first messages for log-based data sources
///
[[nodiscard]] jewels::expected<void, jewels::MonoError> setup_configs(
  std::span<const Tappy<common::ConfigInstanceDescription<>>> descs,
  std::span<const Tappy<common::DataSource<>>> data_sources,
  jewels::memory::MemoryResource memres_sys,
  jewels::memory::MemoryResource memres_cfg,
  const FirstMessageCache& first_message_cache,
  AbstractCasing& casing);

///
/// Invoke casing->try_connect_config for each of the specified connections
///
[[nodiscard]] jewels::expected<void, jewels::MonoError>
connect_configs(std::span<const Tappy<common::ConfigConnection>> connections, AbstractCasing& casing);

} // namespace clockwork::scaffolding
