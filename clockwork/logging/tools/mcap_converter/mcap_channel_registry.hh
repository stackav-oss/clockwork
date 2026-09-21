// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/onboard/types.hh"
#include "jewels/std/expected.hh"

#include <mcap/types.hpp>
#include <mcap/writer.hpp>

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace clockwork_logging
{
/// Class to register schemas and channels from multiple logs with a single writer. Provides
/// an interface to lookup new channel ids from the original log channels.
class McapChannelRegistry
{
public:
  McapChannelRegistry() = default;
  ~McapChannelRegistry() = default;

  McapChannelRegistry(const McapChannelRegistry&) = delete;
  McapChannelRegistry& operator=(const McapChannelRegistry&) = delete;
  McapChannelRegistry(McapChannelRegistry&&) = default;
  McapChannelRegistry& operator=(McapChannelRegistry&&) = default;

  /// Get the channel ID for a channel name
  /// @param[in] channel_name Channel name
  /// @return Channel ID or MonoError if the channel is not registered
  [[nodiscard]] jewels::expected<mcap::ChannelId, jewels::MonoError> try_get_channel_id(std::string_view channel_name);

  /// Register a channel
  /// @param[in] writer MCAP writer
  /// @param[in] metadata Logged channel metadata
  /// @return Channel ID
  [[nodiscard]] mcap::ChannelId
  register_channel(mcap::McapWriter& writer, const onboard::LoggedChannelMetadata& metadata);

private:
  /// Map from channel name to channel ID
  std::unordered_map<std::string, mcap::ChannelId> channel_id_map_;

  /// Map from schema name to schema ID
  std::unordered_map<std::string, mcap::SchemaId> schema_id_map_;

  /// Channels in the registry
  std::vector<mcap::Channel> channels_;

  /// Schemas in the registry
  std::vector<mcap::Schema> schemas_;
};

} // namespace clockwork_logging
