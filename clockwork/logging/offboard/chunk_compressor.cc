// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/offboard/chunk_compressor.hh"

#include "clockwork/logging/nolint_helper.hh"
#include "clockwork/logging/offboard/log_format.hh"
#include "clockwork/logging/xxh3_checksum.hh"
#include "clockwork/logging/zstd_helper.hh"
#include "jewels/std/expected.hh"

#include <array>
#include <cstddef>
#include <cstring>
#include <memory_resource>
#include <span>
#include <utility>
#include <vector>

namespace clockwork_logging::offboard
{

namespace
{

/// Compress a chunk using zstd
/// @param[in] chunk Chunk to compress
/// @param[in] memory_resource Memory resource
/// @return Compressed chunk or LogError on failure
[[nodiscard]] LogExpected<std::pmr::vector<std::byte>>
zstd_compress(std::pmr::vector<std::byte> chunk, jewels::memory::MemoryResource memory_resource)
{
  return clockwork_logging::zstd_compress(std::span{chunk}, memory_resource);
}

/// Decompress a chunk using zstd
/// @param[in] chunk Chunk to compress
/// @param[in] memory_resource Memory resource
/// @return Compressed chunk or LogError on failure
[[nodiscard]] LogExpected<std::pmr::vector<std::byte>>
zstd_decompress(std::pmr::vector<std::byte> chunk, jewels::memory::MemoryResource memory_resource)
{
  auto decompress_result = clockwork_logging::zstd_decompress(std::span{chunk}, memory_resource);
  if (!decompress_result)
  {
    return decompress_result;
  }
  if (decompress_result->size() < chunk_trailer_common_size)
  {
    return jewels::unexpected(LogError::decompression_failure);
  }
  if (decompress_result->size() > max_file_chunk_size)
  {
    return jewels::unexpected(LogError::decompression_failure);
  }
  return decompress_result;
}

} // namespace

ChunkCompressor::ChunkCompressor(jewels::memory::MemoryResource memory_resource) noexcept
  : memory_resource_(std::move(memory_resource))
{
}

[[nodiscard]] LogExpected<std::pmr::vector<std::byte>>
ChunkCompressor::compress_chunk(std::pmr::vector<std::byte> chunk, CompressionType compression_type) const
{
  if (chunk.size() < chunk_trailer_common_size)
  {
    return jewels::unexpected(LogError::invalid_file_chunk);
  }

  auto trailer_span = std::span{&chunk.at(chunk.size() - chunk_trailer_common_size), chunk_trailer_common_size};
  auto trailer_result = nolint_helper::byte_span_to_mutable_value_ptr<ChunkTrailerCommon>(trailer_span);
  if (!trailer_result)
  {
    return jewels::unexpected(trailer_result.error());
  }
  auto* trailer_ptr = trailer_result.value();
  trailer_ptr->compression_type = compression_type;
  std::memset(&trailer_ptr->reserved, 0U, sizeof(trailer_ptr->reserved));
  trailer_ptr->checksum = compute_xxh3_checksum(std::span{chunk.data(), chunk.size() - chunk_trailer_common_size});

  switch (compression_type)
  {
  case CompressionType::none:
    return {std::move(chunk)};
  case CompressionType::zstd:
    return zstd_compress(std::move(chunk), memory_resource_);
  }
}

[[nodiscard]] LogExpected<std::pmr::vector<std::byte>>
ChunkCompressor::decompress_chunk(std::pmr::vector<std::byte> chunk, CompressionType compression_type) const
{
  std::pmr::vector<std::byte> dest_chunk;
  switch (compression_type)
  {
  case CompressionType::none:
    if (chunk.size() < chunk_trailer_common_size)
    {
      return jewels::unexpected(LogError::invalid_file_chunk);
    }
    dest_chunk = std::move(chunk);
    break;
  case CompressionType::zstd:
    auto decompress_result = zstd_decompress(chunk, memory_resource_);
    if (!decompress_result)
    {
      return jewels::unexpected(decompress_result.error());
    }
    dest_chunk = std::move(decompress_result).value();
    break;
  }
  const auto expected_checksum =
    compute_xxh3_checksum(std::span{dest_chunk.data(), dest_chunk.size() - chunk_trailer_common_size});
  auto trailer_span =
    std::span{&dest_chunk.at(dest_chunk.size() - chunk_trailer_common_size), chunk_trailer_common_size};
  const auto trailer_result = nolint_helper::byte_span_to_value_ptr<ChunkTrailerCommon>(trailer_span);
  if (!trailer_result)
  {
    return jewels::unexpected(trailer_result.error());
  }
  const auto* trailer_ptr = trailer_result.value();
  if (
    (trailer_ptr->reserved != decltype(trailer_ptr->reserved){}) ||
    (trailer_ptr->compression_type != compression_type) || (trailer_ptr->checksum != expected_checksum))
  {
    return jewels::unexpected(LogError::decompression_failure);
  }
  return {std::move(dest_chunk)};
}

} // namespace clockwork_logging::offboard
