// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "clockwork/common/abstract_epoll_manager.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/scaffolding/abstract_casing.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"

#include <span>
#include <vector>

namespace clockwork::scaffolding
{

///
/// Instantiate the IO connections in the casing
/// @param descs the instance descriptions
/// @param memres_sys the memory resource used to allocate the return vector
/// @param casing the casing to use
/// @return the list of IO connection instances that are also EPoll callbacks.
///
[[nodiscard]] jewels::expected<std::pmr::vector<jewels::memory::NonNullSharedPtr<EPollable>>, jewels::MonoError>
setup_io_connections(
  std::span<const Tappy<common::IoConnectionInstanceDescription<>>> descs,
  jewels::memory::MemoryResource memres_sys,
  AbstractCasing& casing);

///
/// Bind IO connections to EPoll.
/// @param epollabls A span of IO connections that are epoll callbacks.
/// @param epoll The epoll manager to bind to.
///
void bind_io_connections_to_epoll(
  std::span<jewels::memory::NonNullSharedPtr<EPollable>> epollables, AbstractEPollManager& epoll);

} // namespace clockwork::scaffolding
