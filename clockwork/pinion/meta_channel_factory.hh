// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/pinion/abstract_channel.hh"
#include "clockwork/pinion/abstract_channel_factory.hh"
#include "clockwork/pinion/channel_config_clk_cc.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <memory_resource>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace clockwork::pinion
{

///
/// Factory that can invoke other factories based on config
///
class MetaChannelFactory : public AbstractChannelFactory
{
public:
  ~MetaChannelFactory() override;
  MetaChannelFactory(const MetaChannelFactory&) = delete;
  MetaChannelFactory(MetaChannelFactory&&) = default;
  MetaChannelFactory& operator=(const MetaChannelFactory&) = delete;
  MetaChannelFactory& operator=(MetaChannelFactory&&) = default;

  ///
  /// Create a canonical meta factory using ProcessDescription types for channel configs
  /// @param memres resource used for allocations, only used during construction
  /// @param publisher_configs process description config to extract channel config from
  /// @param nmsp optional namespace for pool isolation, must not include '/'
  /// @param root filesystem root for shared memory (e.g. "/dev/shm")
  /// @param resume_behavior config for channel reconnect behavior
  ///
  static jewels::expected<MetaChannelFactory, jewels::MonoError> make(
    jewels::memory::MemoryResource memres,
    const Tappy<common::ProcessDescription<>>& config,
    std::string_view nmsp,
    std::string_view root,
    AbstractChannel::ResumeBehavior resume_behavior = AbstractChannel::ResumeBehavior::dirty_resume);

  ///
  /// Create a meta factory with a canonical set of backing types given the channel configs
  /// @param memres resource used for allocations, only used during construction
  /// @param channel_types map of channel uuids/names to their respective type
  /// @param nmsp optional namespace for pool isolation, must not include '/'
  /// @param root filesystem root for shared memory (e.g. "/dev/shm")
  /// @param resume_behavior config for channel reconnect behavior
  ///
  static jewels::expected<MetaChannelFactory, jewels::MonoError> make(
    jewels::memory::MemoryResource memres,
    std::pmr::unordered_map<std::pmr::string, ChannelType> channel_types,
    std::pmr::unordered_map<std::pmr::string, std::pmr::vector<std::pmr::string>> publisher_keys,
    std::pmr::unordered_map<std::pmr::string, std::pmr::string> subscriber_keys,
    std::string_view nmsp,
    std::string_view root,
    AbstractChannel::ResumeBehavior resume_behavior = AbstractChannel::ResumeBehavior::dirty_resume);

  ///
  /// Create the factory using per-type sub factories
  ///
  MetaChannelFactory(
    jewels::memory::MemoryResource memres,
    std::pmr::unordered_map<std::pmr::string, ChannelType> channel_types,
    std::map<ChannelType, std::shared_ptr<AbstractChannelFactory>> factories);

  ///
  /// Attempt to open a shared memory channel using the common settings of this factory.
  /// @param uuid_str UUID string name of the channel, used as the filename (in shm_dir)
  /// @param channel_name Human readable channel name
  /// @param layout the BufferLayout to use for the channel's backing buffer
  /// @param max_subscribers maximum size of the in-process and socket observer collections
  ///@{
  jewels::expected<std::shared_ptr<AbstractPublisher>, AbstractChannel::Error> open_publisher(
    std::string_view uuid_str,
    std::string_view channel_name,
    const BufferLayout& layout,
    size_t max_subscribers) override;
  jewels::expected<std::shared_ptr<AbstractSubscriber>, AbstractChannel::Error> open_subscriber(
    std::string_view uuid_str,
    std::string_view channel_name,
    const BufferLayout& layout,
    size_t max_subscribers) override;
  jewels::expected<std::shared_ptr<AbstractSubscriber>, AbstractChannel::Error>
  open_spy(std::string_view uuid_str, std::string_view channel_name, const BufferLayout& layout, size_t max_subscribers)
    override;
  ///@}

private:
  jewels::expected<AbstractChannelFactory*, AbstractChannel::Error> get_factory(std::string_view uuid_str);

  jewels::memory::MemoryResource memres_;
  std::map<ChannelType, std::shared_ptr<AbstractChannelFactory>> factories_;
  std::pmr::unordered_map<std::pmr::string, ChannelType> channel_types_;
};

} // namespace clockwork::pinion
