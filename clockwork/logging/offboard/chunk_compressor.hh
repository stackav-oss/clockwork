// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/log_error.hh"
#include "jewels/memory/memory_resource.hh"

#include <cstddef>
#include <vector>

namespace clockwork_logging::offboard
{

/// Class to compress/decompress chunks before writing to the backing store
///
/// Compression puts an XXH3 checksum of the chunk contents into the common
/// chunk trailer and then compresses the chunk based on the compression type.
///
/// Decompression decompressed the chunk using the specified compression type
/// and then checks the XXH3 checksum in the chunk trailer against the decompressed
/// data.
class ChunkCompressor
{
public:
  /// Constructor
  /// @param[in] memory_resource Memory resource
  explicit ChunkCompressor(jewels::memory::MemoryResource memory_resource) noexcept;
  ~ChunkCompressor() noexcept = default;

  ChunkCompressor(const ChunkCompressor& other) noexcept = default;
  ChunkCompressor& operator=(const ChunkCompressor& other) noexcept = default;
  ChunkCompressor(ChunkCompressor&&) noexcept = default;
  ChunkCompressor& operator=(ChunkCompressor&&) noexcept = default;

  /// Fill in the common chunk trailer and compress the log file chunk
  /// @param[in] chunk Chunk to be compressed
  /// @param[in] compression_type Type of compression to use for the chunk
  /// @return Compressed chunk or LogError on failure
  [[nodiscard]] LogExpected<std::pmr::vector<std::byte>>
  compress_chunk(std::pmr::vector<std::byte> chunk, CompressionType compression_type) const;

  /// Decompress a log file chunk and checks the common chunk trailer
  /// @param[in] chunk Chunk to be decompressed
  /// @param[in] compression_type Type of compression to use for the chunk
  /// @return Decompressed chunk or LogError on failure
  [[nodiscard]] LogExpected<std::pmr::vector<std::byte>>
  decompress_chunk(std::pmr::vector<std::byte> chunk, CompressionType compression_type) const;

private:
  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;
};

} // namespace clockwork_logging::offboard
