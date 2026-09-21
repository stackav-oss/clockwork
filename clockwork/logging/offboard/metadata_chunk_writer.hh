// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/offboard/chunk_compressor.hh"
#include "clockwork/logging/offboard/chunk_writer.hh"
#include "clockwork/logging/offboard/log_format.hh"
#include "clockwork/logging/offboard/reader_types.hh"
#include "jewels/memory/memory_resource.hh"

#include <cstdint>
#include <functional>
#include <memory_resource>
#include <string_view>
#include <unordered_map>

namespace clockwork_logging::offboard
{

/// Class to build the metadata chunk containing the map from channel ID to channel
/// name and the schema definitions for every channel in the log
class MetadataChunkWriter
{
public:
  /// Constructor
  /// @param[in] memory_resource Memory resource
  explicit MetadataChunkWriter(jewels::memory::MemoryResource memory_resource);

  ~MetadataChunkWriter() noexcept = default;

  MetadataChunkWriter(const MetadataChunkWriter& other) = delete;
  MetadataChunkWriter& operator=(const MetadataChunkWriter& other) = delete;
  MetadataChunkWriter(MetadataChunkWriter&&) noexcept = default;
  MetadataChunkWriter& operator=(MetadataChunkWriter&&) noexcept = default;

  /// Add channel metadata to the log
  /// @param[in] metadata Logged channel metadata
  /// @return Channel ID for the channel or LogError on failure
  [[nodiscard]] LogExpected<uint16_t> add_channel(reader::LoggedChannelInfo metadata);

  /// Get the channel ID for a channel name
  /// @param[in] channel_name
  /// @return Channel ID or LogError on failure
  [[nodiscard]] LogExpected<uint16_t> get_channel_id(std::string_view channel_name);

  /// Get the compression type for a channel ID
  /// @param[in] channel_id Channel ID
  /// @return Compression type or LogError on failuer
  [[nodiscard]] LogExpected<CompressionType> get_compression_type(uint16_t channel_id);

  /// Write the chunk to the file
  /// @param[in] chunk_compressor Chunk compressor
  /// @param[in] chunk_writer Chunk writer
  /// @return Location of the written chunk or LogError on failure
  [[nodiscard]] LogExpected<ChunkLocation>
  write_chunk(const ChunkCompressor& chunk_compressor, ChunkWriter& chunk_writer);

private:
  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;

  /// Map from channel name to channel ID
  std::pmr::unordered_map<std::string_view, uint16_t> channel_name_to_id_map_;

  /// Map from channel ID to channel metadata
  std::pmr::unordered_map<uint16_t, reader::LoggedChannelInfo> metadata_map_;
};

} // namespace clockwork_logging::offboard
