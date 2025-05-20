// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/tools/mcap_converter/mcap_channel_registry.hh"

#include <wise_enum.h>

#include <unordered_map>
#include <utility>

namespace clockwork_logging
{

[[nodiscard]] jewels::expected<mcap::ChannelId, jewels::MonoError>
McapChannelRegistry::try_get_channel_id(std::string_view channel_name)
{
  const auto channel_iter = channel_id_map_.find(std::string{channel_name});
  if (channel_iter == channel_id_map_.end())
  {
    return jewels::unexpected(jewels::MonoError{});
  }
  return channel_iter->second;
}

[[nodiscard]] mcap::ChannelId
McapChannelRegistry::register_channel(mcap::McapWriter& writer, const onboard::LoggedChannelMetadata& metadata)
{
  const auto channel_iter = channel_id_map_.find(std::string{metadata.channel_name});
  if (channel_iter != channel_id_map_.end())
  {
    return channel_iter->second;
  }
  auto schema_iter = schema_id_map_.find(std::string{metadata.schema_name});
  if (schema_iter == schema_id_map_.end())
  {
    schemas_.emplace_back(
      metadata.schema_name, wise_enum::to_string(metadata.schema_encoding), metadata.schema_definition);
    writer.addSchema(schemas_.back());
    schema_iter = schema_id_map_.emplace(schemas_.back().name, schemas_.back().id).first;
  }
  channels_.emplace_back(metadata.channel_name, wise_enum::to_string(metadata.message_encoding), schema_iter->second);
  writer.addChannel(channels_.back());
  channel_id_map_.emplace(channels_.back().topic, channels_.back().id);
  return channels_.back().id;
}

} // namespace clockwork_logging
