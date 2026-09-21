// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "clockwork/logging/lite_compressor_interface.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/serialization/cpp/clk_type.hh"
#include "jewels/memory/memory_resource.hh"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace clockwork::serialization
{

/// Class to do schema-aware lite compression/decompression on messages.
///
/// @see LiteCompressorInterface
class TachyonLiteCompressor : public clockwork_logging::LiteCompressorInterface
{
public:
  /// Use make_compressor to create an instance
  /// @param[in] memory_resource Memory resource
  /// @param[in] name Name to use for error messages
  /// @param[in] compressor Schema aware lite compressor
  TachyonLiteCompressor(
    jewels::memory::MemoryResource memory_resource,
    std::string_view name,
    std::shared_ptr<ClkTypeLiteCompressor> compressor);

  ~TachyonLiteCompressor() noexcept override = default;

  TachyonLiteCompressor(const TachyonLiteCompressor&) = delete;
  TachyonLiteCompressor& operator=(const TachyonLiteCompressor&) = delete;
  TachyonLiteCompressor(TachyonLiteCompressor&&) noexcept = default;
  TachyonLiteCompressor& operator=(TachyonLiteCompressor&&) noexcept = default;

  /// @see clockwork_logging::LiteCompressorInterface::compress
  [[nodiscard]] std::span<const std::span<const std::byte>> compress(std::span<const std::byte> data) override;

  /// @see clockwork_logging::LiteCompressorInterface::compress
  void compress(
    jewels::Out<std::span<const std::span<const std::byte>>> compressed_data,
    jewels::Out<uint64_t> counts_checksum,
    jewels::Out<uint64_t> data_checksum,
    std::span<const std::byte> data) override;

  /// @see LiteCompressorInterface::clone
  [[nodiscard]] std::shared_ptr<clockwork_logging::LiteCompressorInterface> clone() const override;

  /// Create a lite-compressor from the schema metadata
  /// @param[in] memory_resource Memory resource
  /// @param[in] name Name to use in error messages
  /// @param[in] metadata Serialized schema metadata
  /// @returns Compressor intstance
  /// @throws runtime_error on failure
  [[nodiscard]] static std::shared_ptr<TachyonLiteCompressor> make_compressor(
    const jewels::memory::MemoryResource& memory_resource, std::string_view name, std::span<const std::byte> metadata);

  /// Create a lite-compressor for a tachyon type
  /// @tparam SchemaType Schema type
  /// @param[in] memory_resource Memory resource
  /// @param[in] name Name to use in error messages
  /// @returns Compressor instance
  template <typename SchemaType>
  [[nodiscard]] static std::shared_ptr<TachyonLiteCompressor>
  make_compressor(const jewels::memory::MemoryResource& memory_resource, std::string_view name = {})
    requires(TachyonType<SchemaType> || TappyType<SchemaType>);

private:
  /// Make a compression result for data that was not compressed
  /// @param[in] data Data to be (not) compressed
  /// @return Span of spans containing the (not) compressed data
  [[nodiscard]] std::span<const std::span<const std::byte>> make_uncompressed_result(std::span<const std::byte> data);

  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;

  /// Name to use in error messages
  std::pmr::string name_;

  /// Schema compressor
  std::shared_ptr<ClkTypeLiteCompressor> compressor_;

  /// Storage for the counts in the compressed message header
  std::pmr::vector<int32_t> counts_;

  /// Storage for the spans returned by the compress method
  std::pmr::vector<std::span<const std::byte>> spans_;

  /// Storage for the zero chunks found by the lite compressor
  std::pmr::vector<ClkZeroChunk> zero_chunks_;
};

} // namespace clockwork::serialization

#include "clockwork/serialization/cpp/tachyon_lite_compressor.inl"
