// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/scaffolding/cog.hh"

#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/scaffolding/abstract_casing.hh"

#include <xxh3.h>

#include <functional>
#include <memory_resource>
#include <string_view>
#include <utility>
#include "clockwork/common/cog_execution_error_clk_cc.hh"
#include "jewels/container/compare.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"

namespace clockwork::scaffolding
{

[[nodiscard]] jewels::expected<CogMap, jewels::MonoError> setup_cogs(
  std::span<const Tappy<common::CogInstanceDescription<>>> descs,
  jewels::memory::MemoryResource memres_sys,
  jewels::memory::MemoryResource memres_exec,
  const std::shared_ptr<AbstractCogQueue>& queue,
  AbstractCasing& casing)
{
  CogMap cogs(memres_sys);
  cogs.reserve(descs.size());
  for (const auto& desc : descs)
  {
    auto cog = casing.try_instantiate_cog(desc, queue, memres_exec);
    if (!cog)
    {
      jewels::log_cerr_error("error creating cog '{}': {}", desc.get_instance_path_name(), cog.error());
      return jewels::unexpected(jewels::MonoError());
    }
    auto insert = cogs.emplace(desc.get_cog_instance_id(), std::move(cog).value());
    if (!insert.second)
    {
      jewels::log_cerr_error("error creating cog, duplicate instance id '{}'", desc.get_cog_instance_id());
      return jewels::unexpected(jewels::MonoError());
    }
  }
  return cogs;
}

[[nodiscard]] jewels::expected<void, jewels::MonoError> init_cogs(
  std::span<const jewels::Uuid<common::CogInstanceId>> ids,
  const CogMap& cogs,
  jewels::time::SyncTime init_time,
  ExecutionMode execution_mode,
  [[maybe_unused]] const std::pmr::unordered_map<jewels::memory::ObjectPtr<AbstractCog>, int16_t>& cog_ptr_to_gpu_ids)
{
  auto cog_execution_mode =
    execution_mode == ExecutionMode::deterministic ? CogExecutionMode::deterministic : CogExecutionMode::online;
  for (const auto& inst_id : ids)
  {
    auto cog_it = cogs.find(inst_id);
    if (cog_it == cogs.end())
    {
      jewels::log_cerr_error("init cog not found '{}'", inst_id);
      return jewels::unexpected(jewels::MonoError());
    }
    const auto& cog = cog_it->second;
    if (!cog->prepare_for_execution(init_time))
    {
      jewels::log_cerr_error("init cog '{}' failed prepare_for_execution", inst_id);
      return jewels::unexpected(jewels::MonoError());
    }
    if (auto res = cog->execute({.start_time = {init_time}, .execution_mode = cog_execution_mode}); !res)
    {
      jewels::log_cerr_error("init cog '{}' execute failed: {}", inst_id, res.error());
      return jewels::unexpected(jewels::MonoError());
    }
  }
  return {};
}

} // namespace clockwork::scaffolding
