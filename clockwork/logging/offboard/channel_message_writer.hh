// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/offboard/async_work_queue.hh"
#include "clockwork/logging/offboard/chunk_compressor.hh"
#include "clockwork/logging/offboard/chunk_writer.hh"
#include "clockwork/logging/offboard/log_format.hh"
#include "clockwork/logging/offboard/message_chunk_writer.hh"
#include "clockwork/logging/offboard/types.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <vector>

namespace clockwork_logging::offboard
{

/// Class to write messages to the log for a single channel.
///
/// Messages are written to the file in chunks using a message chunk writer, and
/// an index for the channel is built referencing the chunks for the channel.
///
/// The index entries are sorted by transmit time
class ChannelMessageWriter
{
public:
  /// Initial number of index entries
  static constexpr size_t initial_index_capacity = 8192U;

  /// Index  growth size in entries
  static constexpr size_t index_growth_size = 2048U;

  /// Constructor
  /// @param[in] memory_resource Memory resource
  /// @param[in] message_chunk_index_format Message chunk index format
  /// @param[in] compression_type Chunk compression type
  /// @param[in] chunk_compressor_ptr Chunk compressor pointer
  /// @param[in] async_work_queue_ptr Async work queue pointer
  /// @param[in] chunk_writer_ptr Chunk writer pointer
  ChannelMessageWriter(
    jewels::memory::MemoryResource memory_resource,
    MessageChunkIndexFormat message_chunk_index_format,
    CompressionType compression_type,
    jewels::memory::NonNullSharedPtr<ChunkCompressor> chunk_compressor_ptr,
    jewels::memory::NonNullSharedPtr<ChunkWriter> chunk_writer_ptr,
    jewels::memory::NonNullSharedPtr<AsyncWorkQueue> async_work_queue_ptr);

  ~ChannelMessageWriter();

  ChannelMessageWriter(const ChannelMessageWriter& other) = delete;
  ChannelMessageWriter& operator=(const ChannelMessageWriter& other) = delete;
  ChannelMessageWriter(ChannelMessageWriter&&) = delete;
  ChannelMessageWriter& operator=(ChannelMessageWriter&&) = delete;

  /// Add a message to the channel log
  /// @param[in] data_size Message data size in bytes
  /// @param[in] message Logged message
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> add_message(size_t data_size, const ZeroCopyLoggedMessage& message);

  /// Finalize the channel index
  /// @post Any partially filled chunk will be flushed
  /// @return Channel index entries or LogError on failure
  [[nodiscard]] LogExpected<std::pmr::vector<IndexChunkIndexEntry>> finalize();

  /// Flush any partially filled chunk
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> flush_current_chunk();

  /// Wait for all pending async compression requests to complete
  void wait_for_pending_async_write_requests();

  /// Check for errors from async write requests
  [[nodiscard]] LogExpected<void> get_async_write_result();

private:
  /// Add an entry to the chunk index
  /// @param[in] entry New index entry
  void add_index_entry(const IndexChunkIndexEntry& entry);

  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;

  /// Message chunk index format
  MessageChunkIndexFormat message_chunk_index_format_;

  /// Compression type
  CompressionType compression_type_;

  /// Chunk compressor pointer
  jewels::memory::NonNullSharedPtr<ChunkCompressor> chunk_compressor_ptr_;

  /// Chunk wrtier pointer
  jewels::memory::NonNullSharedPtr<ChunkWriter> chunk_writer_ptr_;

  /// Async work queue pointer
  jewels::memory::NonNullSharedPtr<AsyncWorkQueue> async_work_queue_ptr_;

  /// Vector of chunk index entries
  std::pmr::vector<IndexChunkIndexEntry> index_;

  /// Message chunk writer pointer
  std::shared_ptr<MessageChunkWriter> message_chunk_writer_ptr_;

  /// Flag set when the chunk has been finalized
  bool is_finalized_{false};

  /// Number of pending async write requests
  size_t num_pending_async_writes_{0U};

  /// Error code from async write requests
  LogExpected<void> async_write_result_{};

  /// Mutex to serialize access to this object
  std::mutex mutex_;

  /// Condition variable signalled when an async write completes
  std::condition_variable condvar_;
};

} // namespace clockwork_logging::offboard
