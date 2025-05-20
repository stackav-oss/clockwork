// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/scaffolding/memory.hh"

#include "jewels/container/compare.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/monitor_resource.hh"
#include "jewels/memory/pmr_shared_ptr.hh"
#include "jewels/std/expected.hh"

#include <xxh3.h>

#include <functional>
#include <memory>
#include <utility>

namespace clockwork::scaffolding
{

using ProcessInstanceUuid = jewels::Uuid<common::ProcessInstanceId>;

jewels::expected<MemResMap, jewels::MonoError> setup_memory_resources(
  std::span<const common::MemoryResourceTap> descs,
  jewels::memory::MemoryResource memres_sys,
  jewels::memory::MemoryResource memres_meta)
{
  MemResMap resources(descs.size(), memres_sys);
  for (const auto& desc : descs)
  {
    switch (desc.get_resource_type())
    {
    case common::MemoryResourceType::new_delete:
      resources[desc.get_memory_resource_id()] = jewels::memory::make_pmr_shared<jewels::memory::MonitorResource>(
        memres_meta, desc.get_resource_max_size(), desc.get_instance_path_name());
      break;
    }
  }
  return std::move(resources);
}

[[nodiscard]] jewels::expected<void, jewels::MonoError> connect_memory_resources(
  std::span<const common::MemoryResourceConnectionTap> connections, const MemResMap& memres_map, AbstractCasing& casing)
{
  for (const auto& connection : connections)
  {
    auto memres_it = memres_map.find(connection.get_memory_resource_id());
    if (memres_it == memres_map.end())
    {
      jewels::log_cerr_error(
        "memory resource connection endpoint '{}' references unknown memory resource '{}'",
        connection.get_endpoint_id(),
        connection.get_memory_resource_id());
      return jewels::unexpected(jewels::MonoError());
    }
    auto result = casing.try_connect_memory_resource(
      connection.get_endpoint_id(), jewels::memory::MemoryResource(memres_it->second.get()));
    if (!result)
    {
      jewels::log_cerr_error(
        "try_connect_memory_resource {} -> {} failed: {}",
        connection.get_memory_resource_id(),
        connection.get_endpoint_id(),
        result.error());
      return jewels::unexpected(jewels::MonoError());
    }
  }
  return {};
}

} // namespace clockwork::scaffolding
