// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/tools/offboard_converter/convert_to_offboard.hh"

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/offboard/types.hh"
#include "clockwork/logging/offboard/writer.hh"
#include "clockwork/logging/readers/abstract_log_reader.hh"
#include "clockwork/logging/readers/log_reader_factory.hh"
#include "clockwork/logging/readers/types.hh"
#include "jewels/log_cerr/log_cerr.hh" // IWYU pragma: keep
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"

#include <fmt/format.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace clockwork_logging
{

namespace
{

/// Open the source log reader
/// @param[in] source_path Source log path
/// @return Source log reader
/// @throws runtime_error on failure
[[nodiscard]] std::unique_ptr<AbstractLogReader> open_reader(std::string_view source_path)
{
  auto reader_ptr = make_reader(source_path, {}, {});
  if (const auto open_result = reader_ptr->open({}); !open_result)
  {
    throw std::runtime_error(fmt::format("Failed to open reader for {}: {}", source_path, open_result.error()));
  }
  return reader_ptr;
}

/// Open the offboard log writer
/// @param[in] offboard_path Offboard log path
/// @param[in] writer_config_pbtxt Offboard writer configuration text protobuf string
/// @return Offboard log writer
/// @throws runtime_error on failure
[[nodiscard]] std::unique_ptr<offboard::Writer<>>
open_offboard_writer(std::string_view offboard_path, std::string_view writer_config_pbtxt)
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  auto writer = std::make_unique<offboard::Writer<>>(memory_resource);
  auto log_path = std::filesystem::path{std::string{offboard_path}};
  if (const auto open_result = writer->open(log_path.string(), writer_config_pbtxt); !open_result)
  {
    const auto err = fmt::format("Failed to open log writer for '{}': {}", log_path.string(), open_result.error());
    throw std::runtime_error(err);
  }
  return writer;
}

/// Create a channel in the offboard log with metadata from the input log
/// @param[in] channel_name Channel name
/// @param[in] reader Input log reader
/// @param[in] writer Offboard log writer
/// @throws runtime_error if an error occurs
void create_offboard_log_channel(
  const std::string& channel_name,
  const std::unordered_map<std::string, TopicMetadata>& metadata_map,
  offboard::Writer<>& writer)
{
  const auto metadata_iter = metadata_map.find(channel_name);
  if (metadata_iter == metadata_map.end())
  {
    throw std::runtime_error(fmt::format("Failed to get metadata for channel {}", channel_name));
  }
  const auto& metadata = metadata_iter->second;
  if (const auto create_result = writer.create_channel(
        offboard::LoggedChannelMetadata{
          .channel_name = metadata.name,
          .message_encoding = metadata.message_encoding,
          .channel_type = metadata.channel_type,
          .schema_name = metadata.type,
          .schema_encoding = metadata.schema_encoding,
          .schema_definition = metadata.schema_definition,
        });
      !create_result)
  {
    const auto err = fmt::format("Failed to create channel {}: {}", channel_name, create_result.error());
    throw std::runtime_error(err);
  }
};

} // namespace

void convert_to_offboard(
  std::string_view onboard_path, std::string_view offboard_path, std::string_view writer_config_pbtxt)
{
  auto reader = open_reader(onboard_path);
  auto writer = open_offboard_writer(offboard_path, writer_config_pbtxt);
  std::unordered_map<std::string, TopicMetadata> metadata_map;
  const auto metadata = reader->get_metadata();
  for (const auto& channel_metadata : metadata)
  {
    metadata_map.emplace(channel_metadata.name, channel_metadata);
  }
  std::unordered_set<std::string> logged_channels;
  while (true)
  {
    const auto read_result = reader->next_message();
    if (!read_result)
    {
      break;
    }
    const auto& logged_msg = *read_result;
    const std::string channel_name{logged_msg.topic};
    if (!logged_channels.contains(channel_name))
    {
      create_offboard_log_channel(channel_name, metadata_map, *writer);
      logged_channels.insert(channel_name);
    }
    // Write the message to the new log.
    const offboard::LoggedMessage msg{
      .channel_name = logged_msg.topic,
      .sequence_number = logged_msg.sequence_number,
      .log_time = logged_msg.log_time,
      .transmit_time = logged_msg.publish_time,
      .header = logged_msg.header,
      .data = logged_msg.data,
    };
    if (const auto write_result = writer->write(msg); !write_result)
    {
      const auto err = fmt::format("Failed to write msg to log: {}", write_result.error());
      throw std::runtime_error(err);
    }
  }
  if (const auto close_result = writer->close(); !close_result)
  {
    const auto err = fmt::format("Failed to close writer: {}", close_result.error());
    throw std::runtime_error(err);
  }
}

} // namespace clockwork_logging
