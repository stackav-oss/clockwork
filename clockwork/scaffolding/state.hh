// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "clockwork/common/process_description.hh"
#include "clockwork/pinion/shm_channel_factory.hh"
#include "clockwork/pinion/shm_publisher.hh"
#include "clockwork/scaffolding/abstract_casing.hh"
#include "clockwork/scaffolding/memory.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"

#include <memory>
#include <span>
#include <vector>

namespace clockwork::scaffolding
{

///
/// Instantiate the requested state objects
/// @param descs the list of state descriptions
/// @param memres_sys used to allocate the channel pointers and return array
/// @param memres_map memory resources to use for state instances that request them
/// @param factory the channel factory used to construct msg-state backing buffers
/// @param casing the casing to invoke try_instantiate_state on
/// @return a set of publishers that back serializable state
///
[[nodiscard]] jewels::expected<std::pmr::vector<std::shared_ptr<pinion::ShmPublisher>>, jewels::MonoError> setup_states(
  std::span<const common::StateInstanceDescriptionTap> descs,
  jewels::memory::MemoryResource memres_sys,
  const MemResMap& memres_map,
  pinion::ShmChannelFactory& factory,
  AbstractCasing& casing);

///
/// Instantiate the requested state objects
/// @param descs the list of state descriptions
/// @param memres_sys the memory resource used to allocate the return vector
/// @param casing the casing to invoke try_instantiate_state on
/// @return the list of cog instances if successful
///
[[nodiscard]] jewels::expected<void, jewels::MonoError> connect_states(
  std::span<const common::StateConnectionTap> connections,
  jewels::memory::MemoryResource memres_sys,
  AbstractCasing& casing);

} // namespace clockwork::scaffolding
