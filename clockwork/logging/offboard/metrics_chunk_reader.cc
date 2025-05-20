// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/offboard/metrics_chunk_reader.hh"

#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/nolint_helper.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"

#include <functional>
#include <memory_resource>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace clockwork_logging::offboard
{

[[nodiscard]] LogExpected<reader::LogMetrics> read_metrics_chunk(
  jewels::memory::MemoryResource memory_resource,
  ChunkLocation metrics_location,
  const std::pmr::unordered_map<uint16_t, reader::LoggedChannelInfo>& channel_info_map,
  ChunkReader& chunk_reader,
  ChunkCompressor& chunk_compressor)
{
  auto read_result = chunk_reader.read_chunk(metrics_location.chunk_offset, metrics_location.chunk_size);
  if (!read_result)
  {
    return jewels::unexpected(read_result.error());
  }
  const auto decompress_result =
    chunk_compressor.decompress_chunk(std::move(read_result).value(), CompressionType::zstd);
  if (!decompress_result)
  {
    jewels::log_cerr_error("Failed to decompress metrics chunk for {}", chunk_reader.file_uri().string());
    return jewels::unexpected(decompress_result.error());
  }
  reader::LogMetrics log_metrics{};
  const auto& metrics_data = decompress_result.value();
  log_metrics.metrics_map = std::pmr::unordered_map<std::pmr::string, reader::LoggedChannelMetrics>{memory_resource};
  const auto trailer_offset = metrics_data.size() - metrics_chunk_trailer_size;
  const auto trailer_result = nolint_helper::byte_span_to_value_ptr<MetricsChunkTrailer>(
    std::span{&metrics_data.at(trailer_offset), metrics_chunk_trailer_size});
  if (!trailer_result)
  {
    return jewels::unexpected(trailer_result.error());
  }
  const auto* trailer_ptr = trailer_result.value();
  const auto channel_entries_size =
    (trailer_offset - trailer_ptr->channel_entries_offset) / metrics_chunk_channel_entry_size;
  const auto channel_entries_span = nolint_helper::byte_span_to_value_span<MetricsChunkChannelEntry>(std::span{
    &metrics_data.at(trailer_ptr->channel_entries_offset), channel_entries_size * metrics_chunk_channel_entry_size});
  if (!channel_entries_span.empty())
  {
    log_metrics.transmit_time_interval = LogInterval{
      LogTimestamp{channel_entries_span.front().min_transmit_time_ns},
      LogTimestamp{channel_entries_span.front().max_transmit_time_ns}};
  }
  for (const auto& channel_entry : channel_entries_span)
  {
    const auto info_iter = channel_info_map.find(channel_entry.channel_id);
    if (info_iter == channel_info_map.end())
    {
      jewels::log_cerr_error(
        "Missing channel info for channel ID {} in {}", channel_entry.channel_id, chunk_reader.file_uri().string());
      return jewels::unexpected(LogError::missing_channel_metadata);
    }
    const LogInterval channel_interval{
      LogTimestamp{channel_entry.min_transmit_time_ns}, LogTimestamp{channel_entry.max_transmit_time_ns}};
    if (log_metrics.message_count == 0U)
    {
      log_metrics.transmit_time_interval = channel_interval;
    }
    else
    {
      log_metrics.transmit_time_interval.add_interval(channel_interval);
    }
    log_metrics.message_count += channel_entry.message_count;
    log_metrics.byte_count += channel_entry.byte_count;
    log_metrics.metrics_map.emplace(
      std::pmr::string{info_iter->second.channel_name, memory_resource},
      reader::LoggedChannelMetrics{
        .message_count = channel_entry.message_count,
        .byte_count = channel_entry.byte_count,
        .transmit_time_interval = channel_interval,
      });
  }
  return {std::move(log_metrics)};
}

} // namespace clockwork_logging::offboard
