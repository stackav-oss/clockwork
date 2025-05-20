// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/offboard/chunk_compressor.hh"
#include "clockwork/logging/offboard/chunk_writer.hh"
#include "clockwork/logging/offboard/log_format.hh"
#include "jewels/memory/memory_resource.hh"

#include <cstdint>
#include <functional>
#include <memory_resource>
#include <unordered_map>

namespace clockwork_logging::offboard
{

/// Class to build the metrics chunk containing the metrics for all of the channels
/// logged in the file
///
/// The metrics chunk contains the start and stop time range for the log and
/// per-channel metrics with the number of messages and number of bytes written to
/// each channel.
class MetricsChunkWriter
{
public:
  /// Constructor
  /// @param[in] memory_resource Memory resource
  explicit MetricsChunkWriter(jewels::memory::MemoryResource memory_resource);

  ~MetricsChunkWriter() noexcept = default;

  MetricsChunkWriter(const MetricsChunkWriter& other) = delete;
  MetricsChunkWriter& operator=(const MetricsChunkWriter& other) = delete;
  MetricsChunkWriter(MetricsChunkWriter&&) noexcept = default;
  MetricsChunkWriter& operator=(MetricsChunkWriter&&) noexcept = default;

  /// Record a message written to a logged channel
  /// @param[in] channel_id Channel ID
  /// @param[in] transmit_time Transmit time
  /// @param[in] uint64_t message_size Message size in bytes
  void count_message(uint16_t channel_id, LogTimestamp transmit_time, uint64_t message_size);

  /// Write the chunk to the file
  /// @post The chunk is empty
  /// @param[in] chunk_compressor Chunk compressor
  /// @param[in] chunk_writer Chunk writer
  /// @return Location of the written chunk or LogError on failure
  [[nodiscard]] LogExpected<ChunkLocation>
  write_chunk(const ChunkCompressor& chunk_compressor, ChunkWriter& chunk_writer);

private:
  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;

  /// Map from channel ID to channel metrics
  std::pmr::unordered_map<uint16_t, MetricsChunkChannelEntry> metrics_map_;
};

} // namespace clockwork_logging::offboard
