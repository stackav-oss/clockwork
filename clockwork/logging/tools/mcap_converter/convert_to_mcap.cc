// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/tools/mcap_converter/convert_to_mcap.hh"

#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/onboard/types.hh"
#include "clockwork/logging/readers/abstract_log_reader.hh"
#include "clockwork/logging/readers/log_reader_factory.hh"
#include "clockwork/logging/readers/types.hh"
#include "clockwork/logging/tools/mcap_converter/mcap_channel_registry.hh"
#include "clockwork/logging/tools/mcap_converter/mcap_util.hh"
#include "jewels/log_cerr/log_cerr.hh" // IWYU pragma: keep
#include "jewels/std/expected.hh"

#include <fmt/format.h>
#include <mcap/errors.hpp>
#include <mcap/types.hpp>
#include <mcap/writer.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
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

/// Open the MCAP log writer
/// @param[in] mcap_path Offboard log path
/// @return MCAP log writer
/// @throws runtime_error on failure
[[nodiscard]] std::unique_ptr<mcap::McapWriter> open_mcap_writer(std::string_view mcap_path)
{
  std::error_code error_code;
  if (!std::filesystem::create_directories(std::string(mcap_path), error_code))
  {
    if (error_code)
    {
      throw std::runtime_error(fmt::format("Failed to create offboard log directory: {}", error_code.message()));
    }
    throw std::runtime_error("Failed to create offboard log directory: File exists");
  }
  auto log_path = std::filesystem::path{std::string{mcap_path}};
  log_path /= log_path.stem();
  log_path.replace_extension(".mcap");

  const auto writer_opts = make_offload_mcap_writer_options();
  auto writer = std::make_unique<mcap::McapWriter>();
  if (const auto status = writer->open(log_path.string(), writer_opts); !status.ok())
  {
    throw std::runtime_error(fmt::format("Failed to open log writer for '{}': {}", mcap_path, status.message));
  }

  return writer;
}

/// Get the merged log channel ID for an input channel name
/// @param[in] channel_name Channel name
/// @param[in] channel_registry MCAP channel registry
/// @param[in] metadata_map Map from channel name to metadata
/// @param[in] writer MCAP merge writer
/// @return MCAP channel ID
[[nodiscard]] mcap::ChannelId get_mcap_channel_id(
  const std::string& channel_name,
  McapChannelRegistry& channel_registry,
  const std::unordered_map<std::string, TopicMetadata>& metadata_map,
  mcap::McapWriter& writer)
{
  const auto channel_id_result = channel_registry.try_get_channel_id(channel_name);
  if (channel_id_result)
  {
    return channel_id_result.value();
  }
  const auto metadata_iter = metadata_map.find(channel_name);
  if (metadata_iter == metadata_map.end())
  {
    throw std::runtime_error(fmt::format("Failed to get metadata for channel '{}'", channel_name));
  }
  const auto& metadata = metadata_iter->second;
  return channel_registry.register_channel(
    writer,
    onboard::LoggedChannelMetadata{
      .channel_name = metadata.name,
      .compression_type = CompressionType::none,
      .message_encoding = metadata.message_encoding,
      .channel_type = metadata.channel_type,
      .schema_name = metadata.type,
      .schema_encoding = metadata.schema_encoding,
      .schema_definition = std::string{metadata.schema_definition},
    });
}

} // namespace

void convert_to_mcap(std::string_view source_path, std::string_view mcap_path)
{
  auto reader = open_reader(source_path);
  auto writer = open_mcap_writer(mcap_path);
  std::unordered_map<std::string, TopicMetadata> metadata_map;
  const auto metadata = reader->get_metadata();
  for (const auto& channel_metadata : metadata)
  {
    metadata_map.emplace(channel_metadata.name, channel_metadata);
  }
  McapChannelRegistry channel_registry;
  while (true)
  {
    const auto read_result = reader->next_message();
    if (!read_result)
    {
      break;
    }
    const auto& logged_msg = *read_result;
    const auto channel_id = get_mcap_channel_id(std::string{logged_msg.topic}, channel_registry, metadata_map, *writer);
    const mcap::Message msg{
      .channelId = channel_id,
      .sequence = logged_msg.sequence_number,
      .logTime = static_cast<uint64_t>(logged_msg.log_time.get_nanoseconds()),
      .publishTime = static_cast<uint64_t>(logged_msg.publish_time.get_nanoseconds()),
      .dataSize = logged_msg.data.size(),
      .data = logged_msg.data.data(),
    };
    if (const auto status = writer->write(msg); !status.ok())
    {
      throw std::runtime_error(fmt::format("Failed to write msg to log: {}", status.message));
    }
  }
  writer->close();
}

} // namespace clockwork_logging
