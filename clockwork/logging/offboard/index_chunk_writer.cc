// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/offboard/index_chunk_writer.hh"

#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/std/expected.hh"

#include <cstddef>
#include <cstring>
#include <memory_resource>
#include <string_view>
#include <utility>
#include <vector>

namespace clockwork_logging::offboard
{

IndexChunkWriter::IndexChunkWriter(jewels::memory::MemoryResource memory_resource)
  : memory_resource_(std::move(memory_resource)), data_(memory_resource_), channel_entries_(memory_resource_)
{
  data_.reserve(initial_buffer_capacity);
  channel_entries_.reserve(initial_channel_entries_capacity);
}

void IndexChunkWriter::add_channel_index(uint16_t channel_id, std::span<const IndexChunkIndexEntry> channel_index)
{
  const auto index_size_bytes = channel_index.size() * index_chunk_index_entry_size;
  reserve_capacity(index_size_bytes);
  const auto chunk_offset = data_.size();
  data_.resize(data_.size() + index_size_bytes);
  if (!channel_index.empty())
  {
    std::memcpy(&data_.at(chunk_offset), channel_index.data(), index_size_bytes);
  }
  add_channel_entry(IndexChunkChannelEntry{
    .channel_id = channel_id,
    .channel_index_offset = static_cast<uint32_t>(chunk_offset),
    .channel_index_size = static_cast<uint32_t>(channel_index.size()),
  });
}

[[nodiscard]] LogExpected<ChunkLocation>
IndexChunkWriter::write_chunk(const ChunkCompressor& chunk_compressor, ChunkWriter& chunk_writer)
{
  const auto channel_entries_offset = data_.size();
  const auto channel_entries_size = channel_entries_.size() * index_chunk_channel_entry_size;
  reserve_capacity(channel_entries_size + index_chunk_trailer_size);
  data_.resize(data_.size() + channel_entries_size);
  if (!channel_entries_.empty())
  {
    std::memcpy(&data_.at(channel_entries_offset), channel_entries_.data(), channel_entries_size);
  }
  IndexChunkTrailer trailer{
    .channel_entries_offset = static_cast<uint32_t>(channel_entries_offset),
  };
  const auto trailer_offset = data_.size();
  data_.resize(data_.size() + index_chunk_trailer_size);
  std::memcpy(&data_.at(trailer_offset), &trailer, index_chunk_trailer_size);
  auto compress_result = chunk_compressor.compress_chunk(std::move(data_), CompressionType::zstd);
  if (!compress_result)
  {
    jewels::log_cerr_error("Failed to compress index chunk for {}", chunk_writer.file_uri().path());
    return jewels::unexpected(compress_result.error());
  }
  reinitialize();
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

void IndexChunkWriter::reserve_capacity(size_t length)
{
  if (data_.size() + length > data_.capacity())
  {
    data_.reserve(data_.size() + length + buffer_growth_size);
  }
}

void IndexChunkWriter::add_channel_entry(const IndexChunkChannelEntry& channel_entry)
{
  if (channel_entries_.size() == channel_entries_.capacity())
  {
    channel_entries_.reserve(channel_entries_.size() + channel_entries_growth_size);
  }
  channel_entries_.push_back(channel_entry);
}

void IndexChunkWriter::reinitialize()
{
  data_ = std::pmr::vector<std::byte>(memory_resource_);
  data_.reserve(initial_buffer_capacity);
  channel_entries_.resize(0U);
}

} // namespace clockwork_logging::offboard
