// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/abstract_epoll_manager.hh"
#include "clockwork/common/process_description.hh"
#include "clockwork/logging/log_writer_config.hh"
#include "clockwork/pinion/observer.hh"
#include "clockwork/pinion/shm_channel.hh"
#include "clockwork/pinion/shm_channel_factory.hh"
#include "clockwork/scaffolding/abstract_casing.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"
#include "jewels/uuid/uuid_hasher.hh"

#include <chrono>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace clockwork::scaffolding
{

///
/// Time to sleep for between checks for the presence of out-of-process publishers
///
constexpr static inline auto channel_connect_sleep_time = std::chrono::milliseconds(200);

using ChannelMap = std::pmr::unordered_map<
  jewels::Uuid<common::EndpointInstanceId>,
  std::shared_ptr<pinion::ShmChannel>,
  jewels::UuidHasher<common::EndpointInstanceId>>;

///
/// Create all the shared memory channels (publishers and subscribers) requested by a process description
/// WARNING: This will block until all required out-of-process publishers are created
/// @param descs list of channel descriptions
/// @param memres memory resource used to allocate the returned map and temporary objects
/// @param process_id the id of this process to determine which channels are published locally vs out-of-process
/// @param factory the channel factory used to generate the channels
/// @return map of all shm channels in the system (subscribers / out-of-process publisher and local publishers)
///
[[nodiscard]] jewels::expected<ChannelMap, jewels::MonoError> setup_channels(
  std::span<const common::PublishEndpointTap> descs,
  jewels::memory::MemoryResource memres,
  const jewels::Uuid<common::ProcessInstanceId>& process_id,
  pinion::ShmChannelFactory& factory);

///
/// Create all the shared memory channels requested by a process description
/// This assumes it is being used in a single process context and therefore all shm channels should be publishers.
/// @param descs list of channel descriptions
/// @param memres memory resource used to allocate the returned map and temporary objects
/// @param factory the channel factory used to generate the channels
/// @return map of all shm channels in the system (subscribers / out-of-process publisher and local publishers)
///
jewels::expected<ChannelMap, jewels::MonoError> setup_deterministic_channels(
  std::span<const common::PublishEndpointTap> descs,
  std::span<const clockwork_logging::LoggedChannelConfigTap> logged_channels,
  jewels::memory::MemoryResource memres,
  pinion::ShmChannelFactory& factory);

///
/// Connect all channel subscribers
/// @param connections list of connections
/// @param memres memory resource used to allocate the returned list
/// @param channels map of all shm channels in the system (subscribers and publishers)
/// @param process_id id of this process, to establish which connections should be made here
/// @param casing the casing to request connections against
/// @return list of channel observers, if successful
///
[[nodiscard]] jewels::expected<std::pmr::vector<std::shared_ptr<pinion::Observer>>, jewels::MonoError>
connect_subscribers(
  std::span<const common::PubSubConnectionTap> connections,
  jewels::memory::MemoryResource memres,
  ChannelMap& channels,
  const jewels::Uuid<common::ProcessInstanceId>& process_id,
  AbstractCasing& casing);

///
/// Connect all channel publishers
/// @param connections list of channel descriptions
/// @param channels map of all shm channels in the system (subscribers and publishers)
/// @param process_id id of this process, to establish which channels are being published in this process
/// @param casing the casing to request connections against
///
[[nodiscard]] jewels::expected<void, jewels::MonoError> connect_publishers(
  std::span<const common::PublishEndpointTap> endpoints,
  ChannelMap& channels,
  const jewels::Uuid<common::ProcessInstanceId>& process_id,
  AbstractCasing& casing);

///
/// Add a channel to the provided AbstractEPollManager instance
/// @param channel Channel to add
/// @param epoll manager to add channel notification callbacks to
/// @throw RuntimeError if sanity checks fail
///
void bind_channel_to_epoll(const std::shared_ptr<pinion::ShmChannel>& channel, AbstractEPollManager& epoll);

///
/// Add channels to the provided AbstractEPollManager instance
/// @param connections set of channels to add
/// @param epoll manager to add channel notification callbacks to
/// @throw RuntimeError if sanity checks fail
///
void bind_channels_to_epoll(const ChannelMap& channels, AbstractEPollManager& epoll);

} // namespace clockwork::scaffolding
