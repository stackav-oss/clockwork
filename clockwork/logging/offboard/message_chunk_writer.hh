// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/offboard/chunk_compressor.hh"
#include "clockwork/logging/offboard/chunk_writer.hh"
#include "clockwork/logging/offboard/log_format.hh"
#include "clockwork/logging/offboard/types.hh"
#include "jewels/math/constants.hh"
#include "jewels/memory/memory_resource.hh"

#include <cstddef>
#include <memory_resource>
#include <vector>

namespace clockwork_logging::offboard
{

/// Class to build message chunks and write them to the log file
///
/// Each message chunk contains a series of logged messages for a single channel followed
/// by an index that contains the transmit timestamp and offset of each message in the
/// chunk and then a trailer that points to the start of the index entries.
///
/// The index entries are sorted by transmit timestamp.
class MessageChunkWriter
{
public:
  /// Buffer growth size in bytes
  static constexpr size_t buffer_growth_size = 1U * jewels::math::constants::bytes_per_mib<size_t>;

  /// Initial buffer size in bytes
  static constexpr size_t initial_buffer_capacity = target_file_chunk_size + buffer_growth_size;

  /// Initial number of index entries
  static constexpr size_t initial_index_capacity = 65536U;

  /// Index  growth size in entries
  static constexpr size_t index_growth_size = 2048U;

  /// Constructor
  /// @param[in] memory_resource Memory resource
  /// @param[in] index_format Message chunk index format
  /// @param[in] compression_type Chunk compression type
  MessageChunkWriter(
    jewels::memory::MemoryResource memory_resource,
    MessageChunkIndexFormat index_format,
    CompressionType compression_type);

  ~MessageChunkWriter() noexcept = default;

  MessageChunkWriter(const MessageChunkWriter& other) = delete;
  MessageChunkWriter& operator=(const MessageChunkWriter& other) = delete;
  MessageChunkWriter(MessageChunkWriter&&) noexcept = default;
  MessageChunkWriter& operator=(MessageChunkWriter&&) noexcept = default;

  /// Test whether the chunk is empty
  [[nodiscard]] bool is_empty() const noexcept;

  /// Test whether the chunk is full
  [[nodiscard]] bool is_full() const noexcept;

  /// Add a message to the chunk
  /// @param[in] data_size Message data size
  /// @param[in] message Message to add to the chunk
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> add_message(size_t data_size, const ZeroCopyLoggedMessage& message);

  /// Write the chunk to the file
  /// @post The chunk is empty
  /// @param[in] chunk_compressor Chunk compressor
  /// @param[in] chunk_writer Chunk writer
  /// @return Index entry for the written chunk or LogError on failure
  [[nodiscard]] LogExpected<IndexChunkIndexEntry>
  write_chunk(const ChunkCompressor& chunk_compressor, ChunkWriter& chunk_writer);

private:
  /// Grow the data buffer to hold the specified number of bytes
  /// @param[in] length Length of the data to be added to the buffer
  void reserve_capacity(size_t length);

  /// Add an entry to the chunk index
  /// @param[in] entry New index entry
  void add_index_entry(const MessageChunkIndexEntryV2& entry);

  /// Reinitialize back to the initial state
  void reinitialize();

  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;

  /// Message chunk index format
  MessageChunkIndexFormat index_format_;

  /// Compression type
  CompressionType compression_type_;

  /// Chunk data buffer
  std::pmr::vector<std::byte> data_;

  /// Vector of message index entries
  std::pmr::vector<MessageChunkIndexEntryV2> index_;

  /// Transmit time interval spanned by the messages in the chunk
  LogInterval transmit_time_interval_;
};

} // namespace clockwork_logging::offboard
