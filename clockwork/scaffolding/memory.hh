// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "clockwork/common/process_description.hh"
#include "clockwork/scaffolding/abstract_casing.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"
#include "jewels/uuid/uuid_hasher.hh"

#include <memory>
#include <memory_resource>
#include <span>
#include <unordered_map>

namespace clockwork::scaffolding
{

using MemResMap = std::pmr::unordered_map<
  jewels::Uuid<common::MemoryResourceId>,
  std::shared_ptr<std::pmr::memory_resource>,
  jewels::UuidHasher<common::MemoryResourceId>>;

///
/// Create all the memory resources requested by a process description
/// @param memres_sys system memory resource, used to allocate the map<>
/// @param memres_meta memory resource used to allocate other memory resources
/// @param descs list of objects describing the memory resources to create
///
[[nodiscard]] jewels::expected<MemResMap, jewels::MonoError> setup_memory_resources(
  std::span<const common::MemoryResourceTap> descs,
  jewels::memory::MemoryResource memres_sys,
  jewels::memory::MemoryResource memres_meta);

///
/// Invoke casing->try_connect_memory_resource for each of the specified connections
///
[[nodiscard]] jewels::expected<void, jewels::MonoError> connect_memory_resources(
  std::span<const common::MemoryResourceConnectionTap> connections,
  const MemResMap& memres_map,
  AbstractCasing& casing);

} // namespace clockwork::scaffolding
