// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/log_playback/tachyon_upgrader.hh"

#include "clockwork/logging/message_encoding.hh"
#include "clockwork/serialization/cpp/tachyon_upgrader.hh"
#include "jewels/memory/pointers.hh"

#include <unordered_map>
#include <utility>

namespace clockwork_logging
{

TachyonUpgrader::TachyonUpgrader(
  jewels::memory::MemoryResource memory_resource,
  const ChannelPublisherConfigTap& publisher_config,
  std::span<const TopicMetadata> metadata)
  : memory_resource_(std::move(memory_resource)), upgrader_map_(memory_resource_), channel_strings_(memory_resource_)
{
  std::pmr::unordered_map<std::string_view, jewels::memory::ObjectPtr<const TopicMetadata>> metadata_map{
    memory_resource_};
  for (const auto& channel_metadata : metadata)
  {
    metadata_map.emplace(channel_metadata.name, jewels::memory::make_non_null_from_ref(channel_metadata));
  }

  for (const auto& channel_config : publisher_config.get_channels())
  {
    const auto metadata_iter = metadata_map.find(channel_config.get_channel_name());
    if (metadata_iter == metadata_map.end())
    {
      continue;
    }
    const auto& channel_metadata = *metadata_iter->second;
    if (channel_metadata.message_encoding != MessageEncoding::tachyon)
    {
      continue;
    }
    auto upgrader = clockwork::serialization::make_tachyon_cpp_upgrader(
      channel_config.get_class_name(),
      channel_config.get_schema_definition(),
      std::as_bytes(std::span{channel_metadata.schema_definition}));
    // NOLINTNEXTLINE(modernize-use-emplace) Compiler does not accept emplace with memory_resource
    channel_strings_.emplace_back(std::pmr::string{channel_config.get_channel_name(), memory_resource_});
    upgrader_map_.emplace(
      channel_strings_.back(),
      UpgraderMapEntry{
        .message_size = channel_config.get_message_size(),
        .upgrader = std::move(upgrader),
      });
  }
}

[[nodiscard]] bool TachyonUpgrader::upgrade_message(
  const std::string_view channel, std::span<const std::byte> input_message, std::pmr::vector<std::byte>& output_message)
{
  const auto upgrader_iter = upgrader_map_.find(channel);
  if (upgrader_iter == upgrader_map_.end())
  {
    return false;
  }
  output_message.resize(upgrader_iter->second.message_size);
  upgrader_iter->second.upgrader->upgrade(input_message, output_message);
  return true;
}

} // namespace clockwork_logging
