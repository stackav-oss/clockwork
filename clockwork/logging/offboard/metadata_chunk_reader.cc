// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/nolint_helper.hh"
#include "clockwork/logging/offboard/chunk_compressor.hh"
#include "clockwork/logging/offboard/chunk_reader.hh"
#include "clockwork/logging/offboard/log_format.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "clockwork/logging/offboard/reader_types.hh"
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

[[nodiscard]] LogExpected<std::pmr::unordered_map<uint16_t, reader::LoggedChannelInfo>> read_metadata_chunk(
  jewels::memory::MemoryResource memory_resource,
  ChunkLocation metadata_location,
  ChunkReader& chunk_reader,
  ChunkCompressor& chunk_compressor)
{
  auto read_result = chunk_reader.read_chunk(metadata_location.chunk_offset, metadata_location.chunk_size);
  if (!read_result)
  {
    return jewels::unexpected(read_result.error());
  }
  const auto decompress_result =
    chunk_compressor.decompress_chunk(std::move(read_result).value(), CompressionType::zstd);
  if (!decompress_result)
  {
    jewels::log_cerr_error("Failed to decompress metadata chunk for {}", chunk_reader.file_uri().string());
    return jewels::unexpected(decompress_result.error());
  }
  const auto& metadata_data = decompress_result.value();
  const auto trailer_offset = metadata_data.size() - metadata_chunk_trailer_size;
  const auto trailer_result = nolint_helper::byte_span_to_value_ptr<const MetricsChunkTrailer>(
    std::span{&metadata_data.at(trailer_offset), metadata_chunk_trailer_size});
  if (!trailer_result)
  {
    return jewels::unexpected(LogError::invalid_argument);
  }
  const auto* trailer_ptr = trailer_result.value();
  const auto channel_entries_size =
    (trailer_offset - trailer_ptr->channel_entries_offset) / metadata_chunk_channel_entry_size;
  const auto channel_entries_span = nolint_helper::byte_span_to_value_span<const MetadataChunkChannelEntry>(std::span{
    &metadata_data.at(trailer_ptr->channel_entries_offset), channel_entries_size * metadata_chunk_channel_entry_size});
  std::pmr::unordered_map<uint16_t, reader::LoggedChannelInfo> channel_info_map{memory_resource};
  for (const auto& channel_entry : channel_entries_span)
  {
    const auto channel_id = channel_entry.channel_id;
    channel_info_map.emplace(
      channel_id,
      reader::LoggedChannelInfo{
        .compression_type = channel_entry.compression_type,
        .channel_name = read_string(
          metadata_data, channel_entry.channel_name_offset, channel_entry.channel_name_size, memory_resource),
        .message_encoding = channel_entry.message_encoding,
        .channel_type = channel_entry.channel_type,
        .schema_name =
          read_string(metadata_data, channel_entry.schema_name_offset, channel_entry.schema_name_size, memory_resource),
        .schema_encoding = channel_entry.schema_encoding,
        .schema_definition = read_string(
          metadata_data, channel_entry.schema_definition_offset, channel_entry.schema_definition_size, memory_resource),
      });
  }
  return {std::move(channel_info_map)};
}

} // namespace clockwork_logging::offboard
