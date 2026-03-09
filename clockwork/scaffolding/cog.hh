// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "clockwork/common/abstract_cog.hh"
#include "clockwork/common/abstract_cog_queue.hh"
#include "clockwork/common/exec_tools.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/scaffolding/abstract_casing.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"
#include "jewels/uuid/uuid_hasher.hh"

#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>

namespace clockwork::scaffolding
{

using CogMap = std::pmr::unordered_map<
  jewels::Uuid<common::CogInstanceId>,
  std::shared_ptr<AbstractCog>,
  jewels::UuidHasher<common::CogInstanceId>>;

///
/// Instantiate the cogs on/in the casing
/// @param descs the instance descriptions
/// @param memres_sys the memory resource used to allocate the return vector
/// @param memres_exec memory resources to be provided to the cogs for internal use
/// @param queue the cog queue to instantiate the cogs against
/// @param casing the casing to use
/// @return the list of cog instances if successful
///
[[nodiscard]] jewels::expected<CogMap, jewels::MonoError> setup_cogs(
  std::span<const Tappy<common::CogInstanceDescription<>>> descs,
  jewels::memory::MemoryResource memres_sys,
  jewels::memory::MemoryResource memres_exec,
  const std::shared_ptr<AbstractCogQueue>& queue,
  AbstractCasing& casing);

///
/// Instantiate the cogs on/in the casing
/// @param ids list of cog ids to run in order
/// @param cogs map of all cogs by id
///
[[nodiscard]] jewels::expected<void, jewels::MonoError> init_cogs(
  std::span<const jewels::Uuid<common::CogInstanceId>> ids,
  const CogMap& cogs,
  jewels::time::SyncTime init_time,
  ExecutionMode execution_mode,
  const std::pmr::unordered_map<jewels::memory::ObjectPtr<AbstractCog>, int16_t>& cog_ptr_to_gpu_ids);

} // namespace clockwork::scaffolding
