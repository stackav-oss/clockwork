// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/offboard/chunk_compressor.hh"
#include "clockwork/logging/offboard/chunk_writer.hh"
#include "clockwork/logging/offboard/log_format.hh"
#include "jewels/memory/memory_resource.hh"

#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <span>
#include <vector>

namespace clockwork_logging::offboard
{

/// Class to build the index chunk containing the indexes for all of the channels
/// logged in the file
///
/// Each channel index is a list of references to the message chunks for the channel.
/// Each reference stores the location of the message chunk and the transmit time
/// range for the messages written to the channel.
///
/// The index entries are sorted by transmit timestamp.
class IndexChunkWriter
{
public:
  /// Initial buffer size in bytes
  static constexpr size_t initial_buffer_capacity = target_file_chunk_size;

  /// Buffer growth size in bytes
  static constexpr size_t buffer_growth_size = target_file_chunk_size;

  /// Initial number of channel entries
  static constexpr size_t initial_channel_entries_capacity = 1024U;

  /// Channels growth size in entries
  static constexpr size_t channel_entries_growth_size = 1024U;

  /// Constructor
  /// @param[in] memory_resource Memory resource
  explicit IndexChunkWriter(jewels::memory::MemoryResource memory_resource);

  ~IndexChunkWriter() noexcept = default;

  IndexChunkWriter(const IndexChunkWriter& other) = delete;
  IndexChunkWriter& operator=(const IndexChunkWriter& other) = delete;
  IndexChunkWriter(IndexChunkWriter&&) noexcept = default;
  IndexChunkWriter& operator=(IndexChunkWriter&&) noexcept = default;

  /// Add an index for a channel to the chunk
  /// @param[in] channel_id Channel ID
  /// @param[in] channel_index Channel index
  /// @return LogError on failure
  void add_channel_index(uint16_t channel_id, std::span<const IndexChunkIndexEntry> channel_index);

  /// Write the chunk to the file
  /// @post The chunk is empty
  /// @param[in] chunk_compressor Chunk compressor
  /// @param[in] chunk_writer Chunk writer
  /// @return Location of the written chunk or LogError on failure
  [[nodiscard]] LogExpected<ChunkLocation>
  write_chunk(const ChunkCompressor& chunk_compressor, ChunkWriter& chunk_writer);

private:
  /// Grow the data buffer to hold the specified number of bytes
  /// @param[in] length Length of the data to be added to the buffer
  void reserve_capacity(size_t length);

  /// Add a channel entry to the index chunk
  /// @param[in] channel_entry New channel entry
  void add_channel_entry(const IndexChunkChannelEntry& channel_entry);

  /// Reinitialize back to the initial state
  void reinitialize();

  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;

  /// Chunk data buffer
  std::pmr::vector<std::byte> data_;

  /// Vector of channel entries
  std::pmr::vector<IndexChunkChannelEntry> channel_entries_;
};

} // namespace clockwork_logging::offboard
