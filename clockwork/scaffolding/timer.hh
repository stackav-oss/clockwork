// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "clockwork/common/abstract_epoll_manager.hh"
#include "clockwork/common/abstract_timer.hh"
#include "clockwork/common/exec_tools.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/pinion/observer.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/scaffolding/abstract_casing.hh"
#include "clockwork/scaffolding/cog.hh"
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

/// Private publisher-throttle timers owned by rate-limited Cogs and runner infrastructure.
using PublisherThrottleTimerVector = std::pmr::vector<std::shared_ptr<AbstractTimer>>;

///
/// Instantiate the requested timers
/// @param descs the timer descriptions
/// @param memres_sys the memory resource used to allocate the return vector
/// @return the list of timer instances if successful
///
[[nodiscard]] jewels::expected<TimerMap, jewels::MonoError> setup_timers(
  std::span<const Tappy<common::TimerInstanceDescription<>>> descs, jewels::memory::MemoryResource memres_sys);

/// Instantiate the requested timers
/// @param execution_params Execution parameters we are running under
/// @param descs the timer descriptions
/// @param memres_sys the memory resource used to allocate the return vector
/// @return the list of timer instances if successful
[[nodiscard]] jewels::expected<TimerMap, jewels::MonoError> setup_deterministic_timers(
  const ExecutionParams& execution_params,
  std::span<const Tappy<common::TimerInstanceDescription<>>> descs,
  jewels::memory::MemoryResource memres_sys);

/// Instantiate and install private online publisher-throttle timers.
/// @param cogs Cogs to inspect for rate-limited publishers.
/// @param memres_sys Memory resource used for the returned vector.
/// @return The installed timer instances on success.
[[nodiscard]] jewels::expected<PublisherThrottleTimerVector, jewels::MonoError>
setup_publisher_throttle_timers(const CogMap& cogs, jewels::memory::MemoryResource memres_sys);

/// Instantiate and install private deterministic publisher-throttle timers.
/// @param cogs Cogs to inspect for rate-limited publishers.
/// @param memres_sys Memory resource used for the returned vector.
/// @return The installed timer instances on success.
[[nodiscard]] jewels::expected<PublisherThrottleTimerVector, jewels::MonoError>
setup_deterministic_publisher_throttle_timers(const CogMap& cogs, jewels::memory::MemoryResource memres_sys);

///
/// Instantiate the requested timers
/// @param descs the timer descriptions
/// @param timers the associated timer instances
/// @param casing the casing to connect into
///
[[nodiscard]] jewels::expected<std::pmr::vector<std::shared_ptr<pinion::Observer>>, jewels::MonoError> connect_timers(
  std::span<const Tappy<common::TimerInstanceDescription<>>> descs,
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

/// Add private publisher-throttle timers to an epoll manager.
/// @param timers Timers to add.
/// @param epoll Manager to receive timer callbacks.
/// @throw RuntimeError if a timer cannot be added.
void bind_timers_to_epoll(const PublisherThrottleTimerVector& timers, AbstractEPollManager& epoll);

} // namespace clockwork::scaffolding
