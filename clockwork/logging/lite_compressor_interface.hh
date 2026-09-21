// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/callsig/outparam.hh"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace clockwork_logging
{

/// Interface implemented by classes that perform lite-compression
///
/// Blocks of consecutive zeros are replaced with a count on compression and expanded
/// on decompression.
///
/// Compressed message format:
///   counts_size: Size of the area that stores the compression bytes counts in bytes (32 bits)
///   message_size: Size of the message in bytes (32 bits)
///   counts: Array of 32 bit counts, positive counts indicate non-zero bytes, negative counts
///           indicate zero bytes.
///   data: Data for the non-zero message bytes
class LiteCompressorInterface
{
public:
  /// Maximum increase in message size after lite compression (message size, chunk count, three chunks = 20 bytes)
  static constexpr size_t max_compression_overhead_bytes = 20U;

  /// Constructor
  LiteCompressorInterface() noexcept = default;

  virtual ~LiteCompressorInterface() noexcept = default;

  LiteCompressorInterface(const LiteCompressorInterface&) = delete;
  LiteCompressorInterface& operator=(const LiteCompressorInterface&) = delete;
  LiteCompressorInterface(LiteCompressorInterface&&) = default;
  LiteCompressorInterface& operator=(LiteCompressorInterface&&) = default;

  /// Compress a buffer by removing blocks of zeros
  ///
  /// The data in the result is backed by the input buffer.
  /// The resulting span remains valid until the next call to compress or decompress.
  ///
  /// @param[in] data Data to be compressed
  /// @return Span of spans containing the compressed data
  [[nodiscard]] virtual std::span<const std::span<const std::byte>> compress(std::span<const std::byte> data) = 0;

  /// Compress a buffer by removing blocks of zeros and generate checksums for the byte counts and compressed data
  ///
  /// The data in the result is backed by the input buffer.
  /// The resulting span remains valid until the next call to compress or decompress.
  ///
  /// @param[out] compressed_data Span of spans containing the compressed data
  /// @param[out] counts_checksum Compressed counts checksum
  /// @param[out] data_checksum Compressed data checksum
  /// @param[in] data Data to be compressed
  virtual void compress(
    jewels::Out<std::span<const std::span<const std::byte>>> compressed_data,
    jewels::Out<uint64_t> counts_checksum,
    jewels::Out<uint64_t> data_checksum,
    std::span<const std::byte> data) = 0;

  /// Make a clone of the compressor
  /// @return Compressor clone
  [[nodiscard]] virtual std::shared_ptr<LiteCompressorInterface> clone() const = 0;
};

} // namespace clockwork_logging
