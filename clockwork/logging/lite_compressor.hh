// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_error.hh"
#include "jewels/memory/memory_resource.hh"

#include <cstddef>
#include <cstdint>
#include <memory_resource>
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

  private:
    /// Data spans
    std::span<const std::span<const std::byte>> data_spans_;

    /// Current span index
    size_t span_index_{0U};

    /// Current span offset
    size_t span_offset_{0U};
  };

public:
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

  /// Decompress a buffer into a destination span
  ///
  /// The destination span size must match the size of the decompressed data
  ///
  /// @param[in] data_spans Data to be decompressed
  /// @param[out] dest_span Decompressed data
  /// @return Decompressed data or MonoError on failure
  [[nodiscard]] LogExpected<std::span<const std::byte>>
  decompress(std::span<const std::span<const std::byte>> data_spans, std::span<std::byte> dest_span);

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

  /// Decompress a buffer into a destination span
  ///
  /// The destination span size must match the size of the decompressed data
  ///
  /// @param[in] data Data to be decompressed
  /// @param[out] dest_span Decompressed data
  /// @return Decompressed data or MonoError on failure
  [[nodiscard]] LogExpected<std::span<const std::byte>>
  decompress(std::span<const std::byte> data, std::span<std::byte> dest_span);

  /// Get the decompressed size of a compressed buffer
  /// @parm[in] data Span containing the compressed buffer
  /// @return Decompressed data size of LogError on failure
  [[nodiscard]] static LogExpected<size_t> get_decompressed_size(std::span<const std::byte> data);

  /// Get the decompressed size of a compressed buffer
  /// @parm[in] data_spans Spans containing the compressed buffer
  /// @return Decompressed data size of LogError on failure
  [[nodiscard]] static LogExpected<size_t>
  get_decompressed_size(std::span<const std::span<const std::byte>> data_spans);

private:
  /// Compress a buffer of uint64_t by removing blocks of zeros
  /// @param[in] aligned_data Aligned data to be compressed
  void compress_aligned_data(std::span<const uint64_t> aligned_data);

  /// Common decompression implementation
  /// @param[in] cursor Source span cursor
  /// @param[in] byte_counts Message byte counts
  /// @param[out] dest_span Destination data span
  /// @return Decompressed data or MonoError on failure
  [[nodiscard]] static LogExpected<std::span<const std::byte>>
  decompress_common(SpanCursor cursor, std::span<const int32_t> byte_counts, std::span<std::byte> dest_span);

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
