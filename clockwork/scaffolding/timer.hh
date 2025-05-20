// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "clockwork/common/abstract_epoll_manager.hh"
#include "clockwork/common/abstract_timer.hh"
#include "clockwork/common/exec_tools.hh"
#include "clockwork/common/process_description.hh"
#include "clockwork/pinion/observer.hh"
#include "clockwork/scaffolding/abstract_casing.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"
#include "jewels/uuid/uuid_hasher.hh"

#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace clockwork::scaffolding
{

using TimerMap = std::pmr::unordered_map<
  jewels::Uuid<common::EndpointInstanceId>,
  std::shared_ptr<AbstractTimer>,
  jewels::UuidHasher<common::EndpointInstanceId>>;

///
/// Instantiate the requested timers
/// @param descs the timer descriptions
/// @param memres_sys the memory resource used to allocate the return vector
/// @return the list of timer instances if successful
///
[[nodiscard]] jewels::expected<TimerMap, jewels::MonoError>
setup_timers(std::span<const common::TimerInstanceDescriptionTap> descs, jewels::memory::MemoryResource memres_sys);

/// Instantiate the requested timers
/// @param execution_params Execution parameters we are running under
/// @param descs the timer descriptions
/// @param memres_sys the memory resource used to allocate the return vector
/// @return the list of timer instances if successful
[[nodiscard]] jewels::expected<TimerMap, jewels::MonoError> setup_deterministic_timers(
  const ExecutionParams& execution_params,
  std::span<const common::TimerInstanceDescriptionTap> descs,
  jewels::memory::MemoryResource memres_sys);

///
/// Instantiate the requested timers
/// @param descs the timer descriptions
/// @param timers the associated timer instances
/// @param casing the casing to connect into
///
[[nodiscard]] jewels::expected<std::pmr::vector<std::shared_ptr<pinion::Observer>>, jewels::MonoError> connect_timers(
  std::span<const common::TimerInstanceDescriptionTap> descs,
  jewels::memory::MemoryResource memres,
  const TimerMap& timers,
  AbstractCasing& casing);

///
/// Add timers to the provided AbstractEPollManager instance
/// @param timers set of timers to add
/// @param epoll manager to add timer notification callbacks to
/// @throw RuntimeError if sanity checks fail
///
void bind_timers_to_epoll(const TimerMap& timers, AbstractEPollManager& epoll);

} // namespace clockwork::scaffolding
