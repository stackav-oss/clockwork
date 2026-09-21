// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/serialization/cpp/tachyon_lite_compressor.hh"

#include "clockwork/logging/xxh3_checksum.hh"
#include "clockwork/serialization/cpp/tachyon_model.hh"
#include "clockwork/serialization/metadata/tachyon_model.pb.h"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_shared_ptr.hh"

#include <fmt/format.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace clockwork::serialization
{

using jewels::InOut;
using jewels::ok;

namespace
{

/// Minimum size of a zero chunk suitable for lite-compression
constexpr size_t min_zero_chunk_size = 9U;

/// Throttle interval for compression error messages
constexpr auto compression_error_message_interval = std::chrono::seconds(5);

} // namespace

TachyonLiteCompressor::TachyonLiteCompressor(
  jewels::memory::MemoryResource memory_resource,
  std::string_view name,
  std::shared_ptr<ClkTypeLiteCompressor> compressor)
  : memory_resource_(std::move(memory_resource)),
    name_(name, memory_resource_),
    compressor_(std::move(compressor)),
    counts_(memory_resource_),
    spans_(memory_resource_),
    zero_chunks_(memory_resource_)
{
}

[[nodiscard]] std::span<const std::span<const std::byte>>
TachyonLiteCompressor::compress(std::span<const std::byte> data)
{
  zero_chunks_.clear();
  // Get the zero chunks from the compressor
  if (!ok(compressor_->compress(data, 0U, InOut{zero_chunks_})))
  {
    thread_local jewels::LogCerrThrottle throttle{compression_error_message_interval};
    jewels::log_cerr_warn_throttled(throttle, "Lite compression failed for {}", name_);
    return make_uncompressed_result(data);
  }
  auto iter = zero_chunks_.begin();
  if (iter == zero_chunks_.end())
  {
    return make_uncompressed_result(data);
  }
  // Chunks are returned from the compressor sorted by offset.
  // Combine adjacent chunks and discard chunks smaller than the mininum size.
  spans_.resize(1U);
  counts_.resize(2U);
  if (iter->offset != 0U)
  {
    // Add span for the data leading up to the first chunk of zeros
    spans_.emplace_back(data.subspan(0U, iter->offset));
    counts_.emplace_back(static_cast<int32_t>(iter->offset));
  }
  auto current_zero_chunk = *iter;
  ++iter;
  while (iter != zero_chunks_.end())
  {
    const auto current_zero_chunk_end_offset = current_zero_chunk.offset + current_zero_chunk.length;
    if (iter->offset == current_zero_chunk_end_offset)
    {
      // Combine the chunk with the current chunk
      current_zero_chunk.length += iter->length;
      ++iter;
      continue;
    }
    if (current_zero_chunk.length >= min_zero_chunk_size)
    {
      // Add a negative count for the current chunk of zeros and add a span for the data between
      // the current chunk of zeros and the one at the iterator
      counts_.emplace_back(-static_cast<int32_t>(current_zero_chunk.length));
      spans_.emplace_back(data.subspan(current_zero_chunk_end_offset, iter->offset - current_zero_chunk_end_offset));
      counts_.emplace_back(static_cast<int32_t>(iter->offset - current_zero_chunk_end_offset));
    }
    else if (spans_.size() == 1U)
    {
      // Chunk is too small and we don't have any data spans, add a span from the start
      // of the data to the offset of the chunk at the iterator
      spans_.emplace_back(data.subspan(0U, iter->offset));
      counts_.emplace_back(static_cast<int32_t>(iter->offset));
    }
    else
    {
      // Add the data from the start of the current chunk to start of the chunk at the iterator
      // to the end of the last chunk in the spans
      spans_.back() = std::span{spans_.back().data(), spans_.back().size() + iter->offset - current_zero_chunk.offset};
      counts_.back() += static_cast<int32_t>(iter->offset - current_zero_chunk.offset);
    }
    current_zero_chunk = *iter;
    ++iter;
  }
  if (current_zero_chunk.length >= min_zero_chunk_size)
  {
    // Add a negative count for the current chunk of zeros and add a span for the data
    // after the current chunk (if any)
    counts_.emplace_back(-static_cast<int32_t>(current_zero_chunk.length));
    const auto current_zero_chunk_end_offset = current_zero_chunk.offset + current_zero_chunk.length;
    if (current_zero_chunk_end_offset != data.size())
    {
      spans_.emplace_back(data.subspan(current_zero_chunk_end_offset, data.size() - current_zero_chunk_end_offset));
      counts_.emplace_back(static_cast<int32_t>(data.size() - current_zero_chunk_end_offset));
    }
  }
  else if (spans_.size() == 1U)
  {
    // The only chunk of zeros was too small so add a span for the entire message
    spans_.emplace_back(data);
    counts_.emplace_back(static_cast<uint32_t>(data.size()));
  }
  else
  {
    // Add the data from the start of the current chunk to the end of the message to
    // the end of the last chunk in the spans
    spans_.back() =
      std::span<const std::byte>{spans_.back().data(), spans_.back().size() + data.size() - current_zero_chunk.offset};
    counts_.back() += static_cast<int32_t>(data.size() - current_zero_chunk.offset);
  }
  // Fill in the counts size and message size
  counts_.at(0) = static_cast<int32_t>(counts_.size() * sizeof(int32_t));
  counts_.at(1) = static_cast<int32_t>(data.size());
  spans_.at(0) = std::as_bytes(std::span{counts_});
  return spans_;
}

[[nodiscard]] std::shared_ptr<clockwork_logging::LiteCompressorInterface> TachyonLiteCompressor::clone() const
{
  return jewels::memory::make_pmr_shared<TachyonLiteCompressor>(memory_resource_, memory_resource_, name_, compressor_);
}

[[nodiscard]] std::span<const std::span<const std::byte>>
TachyonLiteCompressor::make_uncompressed_result(std::span<const std::byte> data)
{
  counts_.resize(3U);
  counts_.at(0U) = 3U * sizeof(uint32_t);
  counts_.at(1U) = static_cast<int32_t>(data.size());
  counts_.at(2U) = static_cast<int32_t>(data.size());
  spans_.resize(2U);
  spans_.at(0U) = std::as_bytes(std::span{counts_});
  spans_.at(1U) = data;
  return spans_;
}

void TachyonLiteCompressor::compress(
  jewels::Out<std::span<const std::span<const std::byte>>> compressed_data,
  jewels::Out<uint64_t> counts_checksum,
  jewels::Out<uint64_t> data_checksum,
  std::span<const std::byte> data)
{
  *compressed_data = compress(data);
  *counts_checksum = clockwork_logging::compute_xxh3_checksum(compressed_data->front());
  *data_checksum = clockwork_logging::compute_xxh3_checksum(compressed_data->subspan(1U));
}

[[nodiscard]] std::shared_ptr<TachyonLiteCompressor> TachyonLiteCompressor::make_compressor(
  const jewels::memory::MemoryResource& memory_resource, std::string_view name, std::span<const std::byte> metadata)
{
  if (metadata.empty())
  {
    const auto msg = fmt::format("Failed to parse empty tachyon metadata for {}", name);
    jewels::log_cerr_error("{}", msg);
    throw std::runtime_error(msg);
  }
  const std::string metadata_str{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) Converting serialized metadata from bytes to string
    reinterpret_cast<const char*>(metadata.data()),
    metadata.size()};
  auto metadata_proto = std::make_unique<metadata::TachyonMetadata>();
  if (!metadata_proto->ParseFromString(metadata_str))
  {
    const auto msg = fmt::format("Failed to parse tachyon metadata for {}", name);
    jewels::log_cerr_error("{}", msg);
    throw std::runtime_error(msg);
  }
  auto model = TachyonModel::from_proto(memory_resource, std::move(metadata_proto));
  auto compressor = model->make_lite_compressor();
  return jewels::memory::make_pmr_shared<TachyonLiteCompressor>(
    memory_resource, memory_resource, name, std::move(compressor));
}

} // namespace clockwork::serialization
