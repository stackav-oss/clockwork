// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/nolint_helper.hh"
#include "clockwork/logging/offboard/chunk_compressor.hh"
#include "clockwork/logging/offboard/log_format.hh"
#include "clockwork/logging/onboard/tests/support/test_support.hh"
#include "clockwork/logging/xxh3_checksum.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <array>
#include <cstddef>
#include <cstring>
#include <memory_resource>
#include <span>
#include <vector>

namespace clockwork_logging::offboard
{
namespace
{

TEST_CASE("ChunkCompressor")
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};

  constexpr auto too_small_chunk_size = 11U;
  const std::pmr::vector<std::byte> too_small_chunk(too_small_chunk_size, memory_resource);
  constexpr auto chunk1_size = 1234U;
  const std::pmr::vector<std::byte> chunk1(chunk1_size, std::byte{'1'}, memory_resource);
  const auto checksum1 = compute_xxh3_checksum(std::span{chunk1}.first(chunk1_size - chunk_trailer_common_size));
  constexpr auto chunk2_size = 2345U;
  std::pmr::vector<std::byte> chunk2(chunk2_size, memory_resource);
  onboard::tests::fill_with_random_bytes(chunk2);
  const auto checksum2 = compute_xxh3_checksum(std::span{chunk2}.first(chunk2_size - chunk_trailer_common_size));

  const ChunkCompressor compressor{memory_resource};

  SECTION("CompressionType::none")
  {
    SECTION("Compress/Decompress chunk1")
    {
      const auto compress_result = compressor.compress_chunk(chunk1, CompressionType::none);
      REQUIRE(compress_result);
      const auto& compress_out = compress_result.value();
      REQUIRE(std::memcmp(chunk1.data(), compress_out.data(), chunk1_size - chunk_trailer_common_size) == 0);
      const auto decompress_result = compressor.decompress_chunk(compress_out, CompressionType::none);
      REQUIRE(decompress_result);
      const auto& decompress_out = decompress_result.value();
      REQUIRE(std::memcmp(chunk1.data(), decompress_out.data(), chunk1_size - chunk_trailer_common_size) == 0);
      const auto trailer_result = nolint_helper::byte_span_to_value_ptr<ChunkTrailerCommon>(
        std::span{&decompress_out.at(chunk1_size - chunk_trailer_common_size), chunk_trailer_common_size});
      REQUIRE(trailer_result);
      const auto* trailer_ptr = trailer_result.value();
      REQUIRE(trailer_ptr->compression_type == CompressionType::none);
      REQUIRE(trailer_ptr->reserved == decltype(trailer_ptr->reserved){});
      REQUIRE(trailer_ptr->checksum == checksum1);
    }

    SECTION("Compress/Decompress chunk2")
    {
      const auto compress_result = compressor.compress_chunk(chunk2, CompressionType::none);
      REQUIRE(compress_result);
      const auto& compress_out = compress_result.value();
      REQUIRE(std::memcmp(chunk2.data(), compress_out.data(), chunk2_size - chunk_trailer_common_size) == 0);
      const auto decompress_result = compressor.decompress_chunk(compress_out, CompressionType::none);
      REQUIRE(decompress_result);
      const auto& decompress_out = decompress_result.value();
      REQUIRE(std::memcmp(chunk2.data(), decompress_out.data(), chunk2_size - chunk_trailer_common_size) == 0);
      const auto trailer_result = nolint_helper::byte_span_to_value_ptr<ChunkTrailerCommon>(
        std::span{&decompress_out.at(chunk2_size - chunk_trailer_common_size), chunk_trailer_common_size});
      REQUIRE(trailer_result);
      const auto* trailer_ptr = trailer_result.value();
      REQUIRE(CompressionType{trailer_ptr->compression_type} == CompressionType::none);
      REQUIRE(trailer_ptr->reserved == decltype(trailer_ptr->reserved){});
      REQUIRE(trailer_ptr->checksum == checksum2);
    }

    SECTION("Chunk too small")
    {
      REQUIRE(
        compressor.compress_chunk(too_small_chunk, CompressionType::none) ==
        jewels::unexpected(LogError::invalid_file_chunk));
      REQUIRE(
        compressor.decompress_chunk(too_small_chunk, CompressionType::none) ==
        jewels::unexpected(LogError::invalid_file_chunk));
    }

    SECTION("Decompress checks the trailer")
    {
      auto compress_result = compressor.compress_chunk(chunk1, CompressionType::none);
      REQUIRE(compress_result);
      auto& compress_out = compress_result.value();
      auto trailer_result = nolint_helper::byte_span_to_mutable_value_ptr<ChunkTrailerCommon>(
        std::span{&compress_out.at(chunk1_size - chunk_trailer_common_size), chunk_trailer_common_size});
      REQUIRE(trailer_result);
      auto* trailer_ptr = trailer_result.value();

      SECTION("Invalid compression type")
      {
        trailer_ptr->compression_type = CompressionType::zstd;
        REQUIRE(
          compressor.decompress_chunk(compress_out, CompressionType::none) ==
          jewels::unexpected(LogError::decompression_failure));
      }

      SECTION("Non zero reserved bytes")
      {
        trailer_ptr->reserved.front() = std::byte{'a'};
        REQUIRE(
          compressor.decompress_chunk(compress_out, CompressionType::none) ==
          jewels::unexpected(LogError::decompression_failure));
      }

      SECTION("Invalid checksum")
      {
        ++trailer_ptr->checksum;
        REQUIRE(
          compressor.decompress_chunk(compress_out, CompressionType::none) ==
          jewels::unexpected(LogError::decompression_failure));
      }
    }
  }

  SECTION("CompressionType::zstd")
  {
    SECTION("Compress/Decompress chunk1")
    {
      const auto compress_result = compressor.compress_chunk(chunk1, CompressionType::zstd);
      REQUIRE(compress_result);
      const auto& compress_out = compress_result.value();
      const auto decompress_result = compressor.decompress_chunk(compress_out, CompressionType::zstd);
      REQUIRE(decompress_result);
      const auto& decompress_out = decompress_result.value();
      REQUIRE(decompress_out.size() == chunk1_size);
      REQUIRE(std::memcmp(chunk1.data(), decompress_out.data(), chunk1_size - chunk_trailer_common_size) == 0);
      const auto trailer_result = nolint_helper::byte_span_to_value_ptr<ChunkTrailerCommon>(
        std::span{&decompress_out.at(chunk1_size - chunk_trailer_common_size), chunk_trailer_common_size});
      REQUIRE(trailer_result);
      const auto* trailer_ptr = trailer_result.value();
      REQUIRE(trailer_ptr->compression_type == CompressionType::zstd);
      REQUIRE(trailer_ptr->reserved == decltype(trailer_ptr->reserved){});
      REQUIRE(trailer_ptr->checksum == checksum1);
    }

    SECTION("Compress/Decompress chunk2")
    {
      const auto compress_result = compressor.compress_chunk(chunk2, CompressionType::zstd);
      REQUIRE(compress_result);
      const auto& compress_out = compress_result.value();
      const auto decompress_result = compressor.decompress_chunk(compress_out, CompressionType::zstd);
      REQUIRE(decompress_result);
      const auto& decompress_out = decompress_result.value();
      REQUIRE(decompress_out.size() == chunk2_size);
      REQUIRE(std::memcmp(chunk2.data(), decompress_out.data(), chunk2_size - chunk_trailer_common_size) == 0);
      const auto trailer_result = nolint_helper::byte_span_to_value_ptr<ChunkTrailerCommon>(
        std::span{&decompress_out.at(chunk2_size - chunk_trailer_common_size), chunk_trailer_common_size});
      REQUIRE(trailer_result);
      const auto* trailer_ptr = trailer_result.value();
      REQUIRE(CompressionType{trailer_ptr->compression_type} == CompressionType::zstd);
      REQUIRE(trailer_ptr->reserved == decltype(trailer_ptr->reserved){});
      REQUIRE(trailer_ptr->checksum == checksum2);
    }

    SECTION("Chunk too small")
    {
      REQUIRE(
        compressor.compress_chunk(too_small_chunk, CompressionType::zstd) ==
        jewels::unexpected(LogError::invalid_file_chunk));
    }

    SECTION("Decompress detects invalid data")
    {
      auto compress_result = compressor.compress_chunk(chunk2, CompressionType::zstd);
      REQUIRE(compress_result);
      auto& compress_out = compress_result.value();
      const auto corrupt_index =
        GENERATE_REF(size_t{0U}, size_t{1U}, size_t{3U}, size_t{11U}, compress_out.size() - 1U);
      compress_out.at(corrupt_index) = ~compress_out.at(corrupt_index);
      REQUIRE(
        compressor.decompress_chunk(compress_out, CompressionType::zstd) ==
        jewels::unexpected(LogError::decompression_failure));
    }
  }
}

} // namespace
} // namespace clockwork_logging::offboard
