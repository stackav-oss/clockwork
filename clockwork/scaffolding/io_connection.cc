// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/scaffolding/io_connection.hh"

#include "clockwork/common/process_description.hh"
#include "clockwork/scaffolding/abstract_casing.hh"
#include "jewels/container/compare.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <memory>
#include <memory_resource>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace clockwork::scaffolding
{

jewels::expected<std::pmr::vector<jewels::memory::NonNullSharedPtr<EPollable>>, jewels::MonoError> setup_io_connections(
  std::span<const common::IoConnectionInstanceDescriptionTap> descs,
  jewels::memory::MemoryResource memres_sys,
  AbstractCasing& casing)
{
  std::pmr::vector<jewels::memory::NonNullSharedPtr<EPollable>> epollables(memres_sys);
  epollables.reserve(descs.size());
  for (const auto& desc : descs)
  {
    std::optional<jewels::Uuid<common::EndpointInstanceId>> diags_id{};
    if (desc.has_diags_endpoint_id())
    {
      diags_id.emplace(desc.value_diags_endpoint_id());
    }
    auto result =
      casing.try_instantiate_io_connection(desc.get_class_id(), desc.get_instance_id(), desc.get_endpoints(), diags_id);
    if (!result)
    {
      jewels::log_cerr_error("error creating IO connection '{}': {}", desc.get_instance_path_name(), result.error());
      return jewels::unexpected(jewels::MonoError());
    }
    auto epollable_ptr = std::move(*result);
    if (epollable_ptr)
    {
      epollables.emplace_back(std::move(epollable_ptr));
    }
  }
  return epollables;
}

void bind_io_connections_to_epoll(
  std::span<jewels::memory::NonNullSharedPtr<EPollable>> epollables, AbstractEPollManager& epoll)
{
  for (const auto& epollable : epollables)
  {
    if (!epollable->register_with(epoll))
    {
      throw std::runtime_error("internal error: could not add event to epoll");
    }
  }
}
} // namespace clockwork::scaffolding
