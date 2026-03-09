// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_error.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/memory/memory_resource.hh"

#include <wise_enum.h>
#include <xxh3.h>

#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <optional>
#include <span>
#include <vector>

namespace clockwork_logging
{

/// Class to do lite compression/decompression on messages.
/// Blocks of consecutive zeros are replaced with a count on compression and expanded
/// on decompression.
///
/// Compressed message format:
///   counts_size: Size of the area that stores the compression bytes counts in bytes (32 bits)
///   message_size: Size of the message in bytes (32 bits)
///   counts: Array of 32 bit counts, positive counts indicate non-zero bytes, negative counts
///           indicate zero bytes.
///   data: Data for the non-zero message bytes
class LiteCompressor
{
  /// Helper class for copying data out of a span of spans
  class SpanCursor
  {
  public:
    /// Constructor
    /// @param[in] data_spans Data spans
    explicit SpanCursor(std::span<const std::span<const std::byte>> data_spans);

    ~SpanCursor() noexcept = default;

    SpanCursor(const SpanCursor&) = default;
    SpanCursor& operator=(const SpanCursor&) = default;
    SpanCursor(SpanCursor&&) = default;
    SpanCursor& operator=(SpanCursor&&) = default;

    /// Copy data out of the data spans into a destination span
    /// @param[in] dest_span Destination data span
    /// @return LogError on failure
    [[nodiscard]] LogExpected<void> copy_out(std::span<std::byte> dest_span);

    /// Get a span covering the next contiguous chunk in the data spans
    /// @param[in] max_size Maximum number of bytes to copy out
    /// @return Data span for the next chunk or LogError on failure
    [[nodiscard]] LogExpected<std::span<const std::byte>> zero_copy_out(size_t byte_count);

    /// @return True if there is no data left to copy
    [[nodiscard]] bool empty() const;

    /// @return Total data size in bytes
    [[nodiscard]] size_t size() const;

  private:
    /// Data spans
    std::span<const std::span<const std::byte>> data_spans_;

    /// Data size in bytes
    size_t size_;

    /// Current span index
    size_t span_index_{0U};

    /// Current span offset
    size_t span_offset_{0U};
  };

public:
  /// Compression mode
  WISE_ENUM_CLASS_MEMBER(
    (CompressionMode, uint8_t),
    // Minimum latency
    minimum_latency,
    // Yield periodically to give other threads a chance to run
    yield_processor)

  /// Maximum increase in message size after lite compression (message size, chunk count, three chunks = 20 bytes)
  static constexpr size_t max_compression_overhead_bytes = 20U;

  /// Constructor
  /// @param[in] memory_resource Memory resource
  explicit LiteCompressor(jewels::memory::MemoryResource memory_resource);

  ~LiteCompressor() noexcept = default;

  LiteCompressor(const LiteCompressor&) = delete;
  LiteCompressor& operator=(const LiteCompressor&) = delete;
  LiteCompressor(LiteCompressor&&) = default;
  LiteCompressor& operator=(LiteCompressor&&) = default;

  /// Compress a buffer by removing blocks of zeros
  ///
  /// The data in the result is backed by the input buffer.
  /// The resulting span remains valid until the next call to compress or decompress.
  ///
  /// @param[in] data Data to be compressed
  /// @return Span of spans containing the compressed data
  [[nodiscard]] std::span<const std::span<const std::byte>> compress(std::span<const std::byte> data);

  /// Compress a buffer by removing blocks of zeros and generate checksums for the byte counts and compressed data
  ///
  /// The data in the result is backed by the input buffer.
  /// The resulting span remains valid until the next call to compress or decompress.
  ///
  /// @param[out] compressed_data Span of spans containing the compressed data
  /// @param[out] counts_checksum Compressed counts checksum
  /// @param[out] data_checksum Compressed data checksum
  /// @param[in] data Data to be compressed
  /// @param[in] mode Compression mode (minimum latency or yield processor)
  void compress(
    jewels::Out<std::span<const std::span<const std::byte>>> compressed_data,
    jewels::Out<uint64_t> counts_checksum,
    jewels::Out<uint64_t> data_checksum,
    std::span<const std::byte> data,
    CompressionMode mode = CompressionMode::minimum_latency);

  /// Decompress a buffer.
  ///
  /// The decompression result remains valid until the next call to decompress or decompress.
  ///
  /// @param[in] data_spans Data to be decompressed
  /// @return Decompressed data or LogError on failure
  [[nodiscard]] LogExpected<std::span<const std::byte>>
  decompress(std::span<const std::span<const std::byte>> data_spans);

  /// Decompress a buffer without copying any data.
  ///
  /// The decompression result remains valid until the next call to decompress or decompress.
  ///
  /// @param[in] data_spans Data to be decompressed
  /// @return Span of spans containing decompressed data or LogError on failure
  [[nodiscard]] LogExpected<std::span<const std::span<const std::byte>>>
  zero_copy_decompress(std::span<const std::span<const std::byte>> data_spans);

  /// Decompress a buffer into a destination span and validate the checksums on the counts and data
  ///
  /// The destination span size must match the size of the decompressed data
  ///
  /// @param[in] counts_checksum Compressed counts checksum
  /// @param[in] data_checksum Compressed data checksum
  /// @param[in] data_span Data to be decompressed
  /// @param[in] dest_span Decompressed data span
  /// @param[in] mode Compression mode (minimum latency or yield processor)
  /// @return Decompressed data or MonoError on failure
  LogOutcome decompress(
    uint64_t counts_checksum,
    uint64_t data_checksum,
    std::span<const std::byte> data,
    std::span<std::byte> dest_span,
    CompressionMode mode = CompressionMode::minimum_latency);

  /// Decompress a buffer.
  ///
  /// The decompression result remains valid until the next call to decompress or decompress.
  ///
  /// @param[in] data Data to be decompressed
  /// @return Decompressed data or LogError on failure
  [[nodiscard]] LogExpected<std::span<const std::byte>> decompress(std::span<const std::byte> data);

  /// Decompress a buffer without copying any data
  ///
  /// The decompression result remains valid until the next call to decompress or decompress.
  ///
  /// @param[in] data Data to be decompressed
  /// @return Decompressed data or LogError on failure
  [[nodiscard]] LogExpected<std::span<const std::span<const std::byte>>>
  zero_copy_decompress(std::span<const std::byte> data);

  /// Get the decompressed size of a compressed buffer
  /// @parm[in] data Span containing the compressed buffer
  /// @return Decompressed data size of LogError on failure
  [[nodiscard]] static LogExpected<size_t> get_decompressed_size(std::span<const std::byte> data);

  /// Get the decompressed size of a compressed buffer
  /// @parm[in] data_spans Spans containing the compressed buffer
  /// @return Decompressed data size of LogError on failure
  [[nodiscard]] static LogExpected<size_t>
  get_decompressed_size(std::span<const std::span<const std::byte>> data_spans);

  /// Update the XXH3 checksum respecting the compression mode
  /// @param[in] data Data
  /// @param[in] mode Compression mode (minimum latency or yield processor)
  static void update_checksum(XXH3_state_t& state, std::span<const std::byte> data, CompressionMode mode);

private:
  /// Compress a buffer of uint64_t by removing blocks of zeros and updating the data checksum
  /// @param[in,out] maybe_data_checksum_state Data checksum state
  /// @param[in] aligned_data Aligned data to be compressed
  /// @param[in] mode Compression mode (minimum latency or yield processor)
  void compress_aligned_data(
    std::optional<XXH3_state_t>& maybe_data_checksum_state,
    std::span<const uint64_t> aligned_data,
    CompressionMode mode);

  /// Common decompression implementation
  /// @param[in,out] maybe_data_checksum_state Optional data checksum state
  /// @param[in] cursor Source span cursor
  /// @param[in] byte_counts Message byte counts
  /// @param[in] dest_span Destination data span
  /// @param[in] mode Compression mode (minimum latency or yield processor)
  /// @return Decompression outcome
  static LogOutcome decompress_common(
    std::optional<XXH3_state_t>& maybe_data_checksum_state,
    SpanCursor cursor,
    std::span<const int32_t> byte_counts,
    std::span<std::byte> dest_span,
    CompressionMode mode);

  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;

  /// Storage to hold the counts of consecutive zero/non-zero bytes in the message, the first count holds the message
  /// size
  std::pmr::vector<int32_t> byte_counts_;

  /// Storage to hold the spans for the compression result
  std::pmr::vector<std::span<const std::byte>> data_spans_;

  /// Buffer used to store decompression results
  std::pmr::vector<std::byte> buffer_;

  /// Buffer used to provide storage for zeros in zero copy decompression results
  std::pmr::vector<std::byte> zeros_buffer_;

  /// Storage for zero copy decompression results
  std::pmr::vector<std::span<const std::byte>> zero_copy_spans_;
};

} // namespace clockwork_logging
