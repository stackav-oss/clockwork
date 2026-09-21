// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/offboard/metrics_chunk_writer.hh"

#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/offboard/chunk_compressor.hh"
#include "clockwork/logging/offboard/chunk_writer.hh"
#include "clockwork/logging/offboard/log_format.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory_resource>
#include <string_view>
#include <utility>
#include <vector>

namespace clockwork_logging::offboard
{

MetricsChunkWriter::MetricsChunkWriter(jewels::memory::MemoryResource memory_resource)
  : memory_resource_(std::move(memory_resource)), metrics_map_(memory_resource_)
{
}

void MetricsChunkWriter::count_message(uint16_t channel_id, LogTimestamp transmit_time, uint64_t message_size)
{
  auto& channel_entry = metrics_map_[channel_id];
  ++channel_entry.message_count;
  channel_entry.channel_id = channel_id;
  channel_entry.byte_count += message_size;
  channel_entry.min_transmit_time_ns = std::min(channel_entry.min_transmit_time_ns, transmit_time.get_nanoseconds());
  channel_entry.max_transmit_time_ns = std::max(channel_entry.max_transmit_time_ns, transmit_time.get_nanoseconds());
}

[[nodiscard]] LogExpected<ChunkLocation>
MetricsChunkWriter::write_chunk(const ChunkCompressor& chunk_compressor, ChunkWriter& chunk_writer)
{
  std::pmr::vector<std::byte> chunk{memory_resource_};
  chunk.reserve((metrics_chunk_channel_entry_size * metrics_map_.size()) + metrics_chunk_trailer_size);
  MetricsChunkTrailer metrics_trailer{};
  metrics_trailer.channel_entries_offset = 0U;
  for (const auto& [channel_id, channel_metrics] : metrics_map_)
  {
    const auto chunk_offset = chunk.size();
    chunk.resize(chunk.size() + metrics_chunk_channel_entry_size);
    std::memcpy(&chunk.at(chunk_offset), &channel_metrics, metrics_chunk_channel_entry_size);
  }
  const auto chunk_offset = chunk.size();
  chunk.resize(chunk.size() + metrics_chunk_trailer_size);
  std::memcpy(&chunk.at(chunk_offset), &metrics_trailer, metrics_chunk_trailer_size);
  auto compress_result = chunk_compressor.compress_chunk(std::move(chunk), CompressionType::zstd);
  if (!compress_result)
  {
    jewels::log_cerr_error("Failed to compress metrics chunk for {}", chunk_writer.file_uri().path());
    return jewels::unexpected(compress_result.error());
  }
  metrics_map_.clear();
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
