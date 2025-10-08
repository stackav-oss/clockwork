// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/offboard/metadata_chunk_writer.hh"

#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/std/expected.hh"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory_resource>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace clockwork_logging::offboard
{

namespace
{

/// Store a string in the chunk and return the offset
/// @param[in] str String to log
/// @param[in,out] chunk Chunk buffer
/// @return Offset to the string location
[[nodiscard]] uint32_t log_string(std::string_view str, std::pmr::vector<std::byte>& chunk)
{
  const auto chunk_offset = chunk.size();
  if (!str.empty())
  {
    chunk.resize(chunk.size() + str.size());
    std::memcpy(&chunk.at(chunk_offset), str.data(), str.size());
  }
  return static_cast<uint32_t>(chunk_offset);
}

} // namespace

MetadataChunkWriter::MetadataChunkWriter(jewels::memory::MemoryResource memory_resource)
  : memory_resource_(std::move(memory_resource)),
    channel_name_to_id_map_(memory_resource_),
    metadata_map_(memory_resource_)
{
}

[[nodiscard]] LogExpected<uint16_t> MetadataChunkWriter::add_channel(reader::LoggedChannelInfo metadata)
{
  if (channel_name_to_id_map_.contains(metadata.channel_name))
  {
    jewels::log_cerr_error("Duplicate channel: {}", metadata.channel_name);
    return jewels::unexpected(LogError::channel_already_exists);
  }
  if (metadata.channel_name.size() > max_name_string_size)
  {
    jewels::log_cerr_error("Channel name exceeds max size ({})", max_name_string_size);
    return jewels::unexpected(LogError::channel_name_exceeds_max_name_size);
  }
  if (metadata.schema_name.size() > max_name_string_size)
  {
    jewels::log_cerr_error("Schema name exceeds max size ({})", max_name_string_size);
    return jewels::unexpected(LogError::schema_name_exceeds_max_name_size);
  }
  if (metadata.schema_definition.size() > max_schema_definition_string_size)
  {
    jewels::log_cerr_error("Schema definition exceeds max size ({})", max_schema_definition_string_size);
    return jewels::unexpected(LogError::schema_definition_exceeds_max_size);
  }
  const auto channel_id = static_cast<uint16_t>(metadata_map_.size()) + 1U;
  const auto metadata_iter = metadata_map_.emplace(channel_id, std::move(metadata)).first;
  channel_name_to_id_map_.emplace(metadata_iter->second.channel_name, channel_id);
  return static_cast<uint16_t>(channel_id);
}

[[nodiscard]] LogExpected<uint16_t> MetadataChunkWriter::get_channel_id(std::string_view channel_name)
{
  const auto iter = channel_name_to_id_map_.find(channel_name);
  if (iter == channel_name_to_id_map_.end())
  {
    return jewels::unexpected(LogError::unknown_channel);
  }
  return iter->second;
}

[[nodiscard]] LogExpected<CompressionType> MetadataChunkWriter::get_compression_type(uint16_t channel_id)
{
  const auto iter = metadata_map_.find(channel_id);
  if (iter == metadata_map_.end())
  {
    return jewels::unexpected(LogError::unknown_channel);
  }
  return iter->second.compression_type;
}

[[nodiscard]] LogExpected<ChunkLocation>
MetadataChunkWriter::write_chunk(const ChunkCompressor& chunk_compressor, ChunkWriter& chunk_writer)
{
  std::pmr::vector<std::byte> chunk{memory_resource_};
  chunk.reserve(target_file_chunk_size);
  std::pmr::vector<MetadataChunkChannelEntry> channel_entries{memory_resource_};
  channel_entries.reserve(metadata_map_.size());
  for (const auto& [channel_id, metadata] : metadata_map_)
  {
    channel_entries.push_back(
      MetadataChunkChannelEntry{
        .channel_id = channel_id,
        .compression_type = metadata.compression_type,
        .message_encoding = metadata.message_encoding,
        .schema_encoding = metadata.schema_encoding,
        .channel_name_offset = log_string(metadata.channel_name, chunk),
        .channel_name_size = static_cast<uint32_t>(metadata.channel_name.size()),
        .schema_name_offset = log_string(metadata.schema_name, chunk),
        .schema_name_size = static_cast<uint32_t>(metadata.schema_name.size()),
        .schema_definition_offset = log_string(metadata.schema_definition, chunk),
        .schema_definition_size = static_cast<uint32_t>(metadata.schema_definition.size()),
        .channel_type = metadata.channel_type,
      });
  }
  chunk.reserve((channel_entries.size() * metadata_chunk_channel_entry_size) + metadata_chunk_trailer_size);
  const auto channel_entries_offset = chunk.size();
  if (!channel_entries.empty())
  {
    chunk.resize(chunk.size() + (channel_entries.size() * metadata_chunk_channel_entry_size));
    std::memcpy(
      &chunk.at(channel_entries_offset),
      channel_entries.data(),
      channel_entries.size() * metadata_chunk_channel_entry_size);
  }
  const auto trailer_offset = chunk.size();
  MetadataChunkTrailer trailer{
    .channel_entries_offset = static_cast<uint32_t>(channel_entries_offset),
  };
  chunk.resize(chunk.size() + metadata_chunk_trailer_size);
  std::memcpy(&chunk.at(trailer_offset), &trailer, metadata_chunk_trailer_size);
  auto compress_result = chunk_compressor.compress_chunk(std::move(chunk), CompressionType::zstd);
  if (!compress_result)
  {
    jewels::log_cerr_error("Failed to compress metadata chunk for {}", chunk_writer.file_uri().path());
    return jewels::unexpected(compress_result.error());
  }
  const auto chunk_size = compress_result.value().size();
  const auto write_result = chunk_writer.write_chunk(std::move(compress_result).value());
  if (!write_result)
  {
    return jewels::unexpected(write_result.error());
  }
  return ChunkLocation{
    .chunk_offset = write_result.value(),
    .chunk_size = static_cast<uint32_t>(chunk_size),
  };
}

} // namespace clockwork_logging::offboard
