// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/offboard/metadata_chunk_reader.hh"

#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/nolint_helper.hh"
#include "clockwork/logging/offboard/chunk_compressor.hh"
#include "clockwork/logging/offboard/chunk_reader.hh"
#include "clockwork/logging/offboard/log_format.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "clockwork/logging/offboard/reader_types.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory_resource>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace clockwork_logging::offboard
{

namespace
{

/// Read a string from the metadata chunk
/// @param[in] chunk Metadata chunk
/// @param[in] offset String offset
/// @param[in] size String size
/// @param[in] memory_resource
/// @return String loaded from the chunk
[[nodiscard]] std::pmr::string read_string(
  std::span<const std::byte> chunk, uint32_t offset, uint32_t size, jewels::memory::MemoryResource memory_resource)
{
  return std::pmr::string{nolint_helper::byte_span_to_string_view(std::span{&chunk[offset], size}), memory_resource};
}

} // namespace

[[nodiscard]] LogOutcome read_metadata_chunk(
  jewels::memory::MemoryResource memory_resource,
  ChunkLocation metadata_location,
  ChunkReader& chunk_reader,
  ChunkCompressor& chunk_compressor,
  const std::optional<std::pmr::unordered_set<std::pmr::string>>& maybe_desired_channels,
  jewels::Out<std::pmr::unordered_map<uint16_t, reader::LoggedChannelInfo>> channel_info_map,
  jewels::Out<std::pmr::unordered_set<uint16_t>> excluded_channel_ids)
{
  auto read_result = chunk_reader.read_chunk(metadata_location.chunk_offset, metadata_location.chunk_size);
  if (!read_result)
  {
    return read_result.error();
  }
  const auto decompress_result =
    chunk_compressor.decompress_chunk(std::move(read_result).value(), CompressionType::zstd);
  if (!decompress_result)
  {
    jewels::log_cerr_error("Failed to decompress metadata chunk for {}", chunk_reader.file_uri().string());
    return decompress_result.error();
  }
  const auto& metadata_data = decompress_result.value();
  const auto trailer_offset = metadata_data.size() - metadata_chunk_trailer_size;
  const auto trailer_result = nolint_helper::byte_span_to_value_ptr<const MetricsChunkTrailer>(
    std::span{&metadata_data.at(trailer_offset), metadata_chunk_trailer_size});
  if (!trailer_result)
  {
    return LogError::invalid_argument;
  }
  const auto* trailer_ptr = trailer_result.value();
  const auto channel_entries_size =
    (trailer_offset - trailer_ptr->channel_entries_offset) / metadata_chunk_channel_entry_size;
  const auto channel_entries_span = nolint_helper::byte_span_to_value_span<const MetadataChunkChannelEntry>(std::span{
    &metadata_data.at(trailer_ptr->channel_entries_offset), channel_entries_size * metadata_chunk_channel_entry_size});
  *channel_info_map = std::pmr::unordered_map<uint16_t, reader::LoggedChannelInfo>{memory_resource};
  *excluded_channel_ids = std::pmr::unordered_set<uint16_t>{memory_resource};
  for (const auto& channel_entry : channel_entries_span)
  {
    const auto channel_id = channel_entry.channel_id;
    auto channel_name =
      read_string(metadata_data, channel_entry.channel_name_offset, channel_entry.channel_name_size, memory_resource);
    if (!maybe_desired_channels || maybe_desired_channels->contains(channel_name))
    {
      channel_info_map->emplace(
        channel_id,
        reader::LoggedChannelInfo{
          .compression_type = channel_entry.compression_type,
          .channel_name = std::move(channel_name),
          .message_encoding = channel_entry.message_encoding,
          .channel_type = channel_entry.channel_type,
          .schema_name = read_string(
            metadata_data, channel_entry.schema_name_offset, channel_entry.schema_name_size, memory_resource),
          .schema_encoding = channel_entry.schema_encoding,
          .schema_definition = read_string(
            metadata_data,
            channel_entry.schema_definition_offset,
            channel_entry.schema_definition_size,
            memory_resource),
          .is_amended = channel_entry.flags.is_amended != 0,
        });
    }
    else
    {
      excluded_channel_ids->emplace(channel_id);
    }
  }
  return LogError::success;
}

} // namespace clockwork_logging::offboard
