// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "clockwork/logging/channel_publisher_config_clk_cc.hh"
#include "clockwork/logging/readers/types.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/serialization/cpp/tachyon_upgrader.hh"
#include "jewels/memory/memory_resource.hh"

#include <cstddef>
#include <functional>
#include <list>
#include <memory>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace clockwork_logging
{

/// Class to handle schema upgrades for tachyon classes.
class TachyonUpgrader
{
  /// Schema upgrader map entry
  struct UpgraderMapEntry
  {
    /// Message size
    size_t message_size;

    /// Schema upgrader
    std::shared_ptr<clockwork::serialization::TachyonUpgrader> upgrader;
  };

public:
  /// Constructor
  /// @param[in] memory_resource Memory resource
  /// @param[in] publisher_config Channel publisher configuration
  /// @param[in metadata Incoming log metadata
  TachyonUpgrader(
    jewels::memory::MemoryResource memory_resource,
    const clockwork::Tappy<ChannelPublisherConfig<>>& publisher_config,
    std::span<const TopicMetadata> metadata);

  ~TachyonUpgrader() = default;

  TachyonUpgrader(const TachyonUpgrader&) = delete;
  TachyonUpgrader& operator=(const TachyonUpgrader&) = delete;
  TachyonUpgrader(TachyonUpgrader&&) = delete;
  TachyonUpgrader& operator=(TachyonUpgrader&&) = delete;

  /// Upgrade a message on the specified channel into a vector of bytes
  ///
  /// If the upgrade fails the channel is removed from the upgrader map
  ///
  /// @param[in] channel Channel name
  /// @param[in] input_message Incoming message data
  /// @param[in] output_message Upgraded message data
  /// @return True of the upgrade was successful, otherwise false
  [[nodiscard]] bool upgrade_message(
    std::string_view channel, std::span<const std::byte> input_message, std::pmr::vector<std::byte>& output_message);

private:
  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;

  /// Map from channel name to schema upgrader
  std::pmr::unordered_map<std::string_view, UpgraderMapEntry> upgrader_map_;

  /// Storage for the keys in the upgrader map
  std::pmr::list<std::pmr::string> channel_strings_;
};

} // namespace clockwork_logging
