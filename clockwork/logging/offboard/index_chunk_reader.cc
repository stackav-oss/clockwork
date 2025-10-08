// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/offboard/index_chunk_reader.hh"

#include "clockwork/logging/channel_type.hh"
#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/nolint_helper.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"

#include <cstddef>
#include <functional>
#include <iterator>
#include <memory>
#include <memory_resource>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace clockwork_logging::offboard
{

namespace
{

/// Channel index entry
struct ChannelEntry
{
  /// Channel name
  std::string_view channel_name;

  /// Channel type
  ChannelType channel_type{};

  /// Compression type
  CompressionType compression_type{};

  /// Channel index
  std::span<const IndexChunkIndexEntry> channel_index;
};

/// Read the channel index entries for the channels we want to read from the log
/// @param[in] memory_resource Memory resource
/// @param[in] channel_info_map Information on the channels logged in this file
/// @param[in] maybe_desired_channels Optional set of desired channels
/// @param[in] index_data Index chunk data
/// @param[in] file_uri Log file URI
/// @return Vector of channel index entries or LogError on failure
LogExpected<std::pmr::vector<ChannelEntry>> read_channel_entries(
  jewels::memory::MemoryResource memory_resource,
  const std::pmr::unordered_map<uint16_t, reader::LoggedChannelInfo>& channel_info_map,
  const std::optional<std::pmr::unordered_set<std::pmr::string>>& maybe_desired_channels,
  const std::pmr::vector<std::byte>& index_data,
  const LogUri& file_uri)
{
  const auto trailer_offset = index_data.size() - index_chunk_trailer_size;
  const auto trailer_result = nolint_helper::byte_span_to_value_ptr<IndexChunkTrailer>(
    std::span{&index_data.at(trailer_offset), index_chunk_trailer_size});
  if (!trailer_result)
  {
    return jewels::unexpected(trailer_result.error());
  }
  const auto* trailer_ptr = trailer_result.value();
  const auto channel_entries_size =
    (trailer_offset - trailer_ptr->channel_entries_offset) / index_chunk_channel_entry_size;
  const auto channel_entries_span = nolint_helper::byte_span_to_value_span<IndexChunkChannelEntry>(std::span{
    &index_data.at(trailer_ptr->channel_entries_offset), channel_entries_size * index_chunk_channel_entry_size});
  std::pmr::vector<ChannelEntry> channel_entries(memory_resource);
  channel_entries.reserve(channel_entries_size);
  for (const auto& channel_entry : channel_entries_span)
  {
    const auto info_iter = channel_info_map.find(channel_entry.channel_id);
    if (info_iter == channel_info_map.end())
    {
      jewels::log_cerr_error(
        "Missing channel info for channel ID {} in {}", channel_entry.channel_id, file_uri.string());
      return jewels::unexpected(LogError::missing_channel_metadata);
    }
    const auto& channel_info = info_iter->second;
    if (maybe_desired_channels && !maybe_desired_channels->contains(channel_info.channel_name))
    {
      continue;
    }
    const auto channel_index_result = nolint_helper::byte_span_to_value_ptr<IndexChunkIndexEntry>(
      std::span{&index_data.at(channel_entry.channel_index_offset), index_chunk_index_entry_size});
    if (!channel_index_result)
    {
      return jewels::unexpected(channel_index_result.error());
    }
    const auto* channel_index_ptr = channel_index_result.value();
    channel_entries.push_back(
      ChannelEntry{
        .channel_name = channel_info.channel_name,
        .channel_type = channel_info.channel_type,
        .compression_type = channel_info.compression_type,
        .channel_index = std::span{channel_index_ptr, channel_entry.channel_index_size},
      });
  }
  return channel_entries;
}

} // namespace

[[nodiscard]] LogExpected<std::pmr::list<reader::MessageChunkHandle>> read_index_chunk(
  jewels::memory::MemoryResource memory_resource,
  ChunkLocation index_location,
  const std::pmr::unordered_map<uint16_t, reader::LoggedChannelInfo>& channel_info_map,
  const std::optional<LogInterval>& maybe_log_interval,
  const std::optional<std::pmr::unordered_set<std::pmr::string>>& maybe_desired_channels,
  const jewels::memory::NonNullSharedPtr<ChunkReader>& chunk_reader_ptr,
  const jewels::memory::NonNullSharedPtr<ChunkCompressor>& chunk_compressor_ptr)
{
  auto read_result = chunk_reader_ptr->read_chunk(index_location.chunk_offset, index_location.chunk_size);
  if (!read_result)
  {
    return jewels::unexpected(read_result.error());
  }
  const auto decompress_result =
    chunk_compressor_ptr->decompress_chunk(std::move(read_result).value(), CompressionType::zstd);
  if (!decompress_result)
  {
    jewels::log_cerr_error("Failed to decompress index chunk for {}", chunk_reader_ptr->file_uri().string());
    return jewels::unexpected(decompress_result.error());
  }
  const auto channel_entries_result = read_channel_entries(
    memory_resource, channel_info_map, maybe_desired_channels, decompress_result.value(), chunk_reader_ptr->file_uri());
  if (!channel_entries_result)
  {
    return jewels::unexpected(channel_entries_result.error());
  }
  std::pmr::list<reader::MessageChunkHandle> chunk_handles(memory_resource);
  for (const auto& channel_entry : channel_entries_result.value())
  {
    auto index_iter = channel_entry.channel_index.begin();
    while (index_iter != channel_entry.channel_index.end())
    {
      const auto next_index_iter = std::next(index_iter);
      const LogInterval chunk_interval{
        LogTimestamp{index_iter->min_transmit_time_ns}, LogTimestamp{index_iter->max_transmit_time_ns}};
      bool chunk_is_read = false;
      if (!maybe_log_interval || maybe_log_interval->overlaps(chunk_interval))
      {
        chunk_is_read = true;
      }
      else if (channel_entry.channel_type == ChannelType::persistent)
      {
        // The channel index is sorted by min transmit time
        chunk_is_read =
          (chunk_interval.get_start_timestamp() < maybe_log_interval->get_start_timestamp()) &&
          ((next_index_iter == channel_entry.channel_index.end()) ||
           next_index_iter->min_transmit_time_ns > maybe_log_interval->get_start_timestamp().get_nanoseconds());
      }
      if (chunk_is_read)
      {
        chunk_handles.push_back(
          reader::MessageChunkHandle{
            .chunk_handle =
              reader::ChunkHandle{
                .compression_type = channel_entry.compression_type,
                .location = index_iter->location,
                .chunk_reader_ptr = chunk_reader_ptr.get(),
                .chunk_compressor_ptr = chunk_compressor_ptr.get(),
              },
            .channel_name = channel_entry.channel_name,
            .channel_type = channel_entry.channel_type,
            .min_transmit_time = chunk_interval.get_start_timestamp(),
            .maybe_log_interval = maybe_log_interval,
          });
      }
      index_iter = next_index_iter;
    }
  }
  chunk_handles.sort();
  return {std::move(chunk_handles)};
}

} // namespace clockwork_logging::offboard
