// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/lite_compressor.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/xxh3_checksum.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory_resource>
#include <numeric>
#include <ranges>
#include <span>
#include <vector>

namespace clockwork_logging
{
namespace
{

/// Utility to get the size of a span of spans
/// @param[in] data_spans Span of data spans
/// @return Total size of the data spans
[[nodiscard]] size_t spans_size(std::span<const std::span<const std::byte>> data_spans)
{
  return std::accumulate(
    data_spans.begin(), data_spans.end(), size_t{0U}, [](size_t lhs, auto& rhs) { return lhs + rhs.size(); });
}

TEST_CASE("Lite compressor")
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  LiteCompressor compressor{memory_resource};
  LiteCompressor decompressor{memory_resource};

  SECTION("Empty message")
  {
    auto compressed_spans = compressor.compress(std::span<const std::byte>{});
    REQUIRE(spans_size(compressed_spans) == 8U);
    REQUIRE(compressed_spans.size() == 1U);
    REQUIRE(LiteCompressor::get_decompressed_size(compressed_spans) == 0U);
    const auto decompress_result = decompressor.decompress(compressed_spans);
    REQUIRE(decompress_result);
    REQUIRE(decompress_result->empty());
    const auto zero_copy_decompress_result = decompressor.zero_copy_decompress(compressed_spans);
    REQUIRE(zero_copy_decompress_result);
    REQUIRE(zero_copy_decompress_result->empty());

    SECTION("With checksum")
    {
      std::span<const std::span<const std::byte>> checksum_spans;
      uint64_t counts_checksum{};
      uint64_t data_checksum{};
      compressor.compress(
        jewels::Out{checksum_spans},
        jewels::Out{counts_checksum},
        jewels::Out{data_checksum},
        std::span<const std::byte>{});
      const auto checksum_view = checksum_spans | std::views::join;
      REQUIRE(std::ranges::equal(compressed_spans | std::views::join, checksum_view));
      REQUIRE(counts_checksum == compute_xxh3_checksum(compressed_spans.first(1U)));
      REQUIRE(data_checksum == compute_xxh3_checksum(compressed_spans.subspan(1U)));
      std::vector<std::byte> compressed_buffer(checksum_view.begin(), checksum_view.end());
      std::vector<std::byte> dest_buffer;
      const auto decompress_outcome =
        decompressor.decompress(counts_checksum, data_checksum, compressed_buffer, dest_buffer);
      REQUIRE(decompress_outcome.get() == LogError::success);
      REQUIRE(
        decompressor.decompress(counts_checksum + 1U, data_checksum, compressed_buffer, dest_buffer).get() ==
        LogError::bad_checksum);
      REQUIRE(
        decompressor.decompress(counts_checksum, data_checksum + 1U, compressed_buffer, dest_buffer).get() ==
        LogError::bad_checksum);
    }
  }

  SECTION("Small message misaligned")
  {
    constexpr auto buffer_size = 1U + sizeof(uint64_t);
    const auto fill_byte = GENERATE(std::byte{0U}, std::byte{1U});
    alignas(sizeof(uint64_t)) const std::array<std::byte, buffer_size> buffer{fill_byte};
    const auto misalignment = GENERATE(0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U);
    const auto misaligned_buffer = std::span{buffer}.subspan(misalignment, 2U);
    const auto compressed_spans = compressor.compress(misaligned_buffer);
    REQUIRE(compressed_spans.size() == 2U);
    REQUIRE(LiteCompressor::get_decompressed_size(compressed_spans) == misaligned_buffer.size());
    REQUIRE(spans_size(compressed_spans) == (3U * sizeof(int32_t)) + misaligned_buffer.size());

    SECTION("With checksum")
    {
      std::span<const std::span<const std::byte>> checksum_spans;
      uint64_t counts_checksum{};
      uint64_t data_checksum{};
      compressor.compress(
        jewels::Out{checksum_spans}, jewels::Out{counts_checksum}, jewels::Out{data_checksum}, {misaligned_buffer});
      const auto checksum_view = checksum_spans | std::views::join;
      REQUIRE(std::ranges::equal(compressed_spans | std::views::join, checksum_view));
      REQUIRE(counts_checksum == compute_xxh3_checksum(compressed_spans.first(1U)));
      REQUIRE(data_checksum == compute_xxh3_checksum(compressed_spans.subspan(1U)));
      std::vector<std::byte> compressed_buffer(checksum_view.begin(), checksum_view.end());
      std::vector<std::byte> dest_buffer(misaligned_buffer.size());
      const auto decompress_outcome =
        decompressor.decompress(counts_checksum, data_checksum, compressed_buffer, dest_buffer);
      REQUIRE(decompress_outcome.get() == LogError::success);
      REQUIRE(std::memcmp(misaligned_buffer.data(), dest_buffer.data(), dest_buffer.size()) == 0);
      REQUIRE(
        decompressor.decompress(counts_checksum + 1U, data_checksum, compressed_buffer, dest_buffer).get() ==
        LogError::bad_checksum);
      REQUIRE(
        decompressor.decompress(counts_checksum, data_checksum + 1U, compressed_buffer, dest_buffer).get() ==
        LogError::bad_checksum);
    }

    SECTION("Decompress into internal buffer")
    {
      const auto decompress_result = decompressor.decompress(compressed_spans);
      REQUIRE(decompress_result);
      REQUIRE(decompress_result->size() == misaligned_buffer.size());
      REQUIRE(std::memcmp(misaligned_buffer.data(), decompress_result->data(), misaligned_buffer.size()) == 0);
    }

    SECTION("Zero copy decompress")
    {
      const auto decompress_result = decompressor.zero_copy_decompress(compressed_spans);
      REQUIRE(decompress_result);
      REQUIRE(spans_size(decompress_result.value()) == misaligned_buffer.size());
      REQUIRE(std::ranges::equal(decompress_result.value() | std::views::join, misaligned_buffer));
    }
  }

  SECTION("All zeros")
  {
    constexpr auto buffer_size = 128U;
    alignas(sizeof(uint64_t)) const std::array<std::byte, buffer_size> buffer{};
    const auto compressed_spans = compressor.compress({buffer});
    REQUIRE(compressed_spans.size() == 1U);
    REQUIRE(spans_size(compressed_spans) == 3U * sizeof(uint32_t));
    REQUIRE(LiteCompressor::get_decompressed_size(compressed_spans) == buffer_size);

    SECTION("With checksum")
    {
      std::span<const std::span<const std::byte>> checksum_spans;
      uint64_t counts_checksum{};
      uint64_t data_checksum{};
      compressor.compress(
        jewels::Out{checksum_spans}, jewels::Out{counts_checksum}, jewels::Out{data_checksum}, {buffer});
      const auto checksum_view = checksum_spans | std::views::join;
      REQUIRE(std::ranges::equal(compressed_spans | std::views::join, checksum_view));
      REQUIRE(counts_checksum == compute_xxh3_checksum(compressed_spans.first(1U)));
      REQUIRE(data_checksum == compute_xxh3_checksum(compressed_spans.subspan(1U)));
      std::vector<std::byte> compressed_buffer(checksum_view.begin(), checksum_view.end());
      std::vector<std::byte> dest_buffer(buffer.size());
      const auto decompress_outcome =
        decompressor.decompress(counts_checksum, data_checksum, compressed_buffer, dest_buffer);
      REQUIRE(decompress_outcome.get() == LogError::success);
      REQUIRE(std::memcmp(buffer.data(), dest_buffer.data(), buffer_size) == 0);
      REQUIRE(
        decompressor.decompress(counts_checksum + 1U, data_checksum, compressed_buffer, dest_buffer).get() ==
        LogError::bad_checksum);
      REQUIRE(
        decompressor.decompress(counts_checksum, data_checksum + 1U, compressed_buffer, dest_buffer).get() ==
        LogError::bad_checksum);
    }

    SECTION("Decompress into internal buffer")
    {
      const auto decompress_result = decompressor.decompress(compressed_spans);
      REQUIRE(decompress_result);
      REQUIRE(decompress_result->size() == buffer_size);
      REQUIRE(std::memcmp(buffer.data(), decompress_result->data(), buffer_size) == 0);
    }

    SECTION("Zero copy decompress")
    {
      const auto decompress_result = decompressor.zero_copy_decompress(compressed_spans);
      REQUIRE(decompress_result);
      REQUIRE(spans_size(decompress_result.value()) == buffer.size());
      REQUIRE(std::ranges::equal(decompress_result.value() | std::views::join, buffer));
    }
  }

  SECTION("All zeros misaligned")
  {
    constexpr auto buffer_size = 128U;
    alignas(sizeof(uint64_t)) const std::array<std::byte, buffer_size> buffer{};
    const std::span misaligned_buffer{&buffer.at(1U), buffer_size - 2U};
    const auto compressed_spans = compressor.compress(misaligned_buffer);
    REQUIRE(compressed_spans.size() == 3U);
    REQUIRE(spans_size(compressed_spans) == 20U + (2U * (sizeof(uint64_t) - 1U)));
    REQUIRE(LiteCompressor::get_decompressed_size(compressed_spans) == misaligned_buffer.size());

    SECTION("With checksum")
    {
      std::span<const std::span<const std::byte>> checksum_spans;
      uint64_t counts_checksum{};
      uint64_t data_checksum{};
      compressor.compress(
        jewels::Out{checksum_spans}, jewels::Out{counts_checksum}, jewels::Out{data_checksum}, {misaligned_buffer});
      const auto checksum_view = checksum_spans | std::views::join;
      REQUIRE(std::ranges::equal(compressed_spans | std::views::join, checksum_view));
      REQUIRE(counts_checksum == compute_xxh3_checksum(compressed_spans.first(1U)));
      REQUIRE(data_checksum == compute_xxh3_checksum(compressed_spans.subspan(1U)));
      std::vector<std::byte> compressed_buffer(checksum_view.begin(), checksum_view.end());
      std::vector<std::byte> dest_buffer(misaligned_buffer.size());
      const auto decompress_outcome =
        decompressor.decompress(counts_checksum, data_checksum, compressed_buffer, dest_buffer);
      REQUIRE(decompress_outcome.get() == LogError::success);
      REQUIRE(std::memcmp(buffer.data(), dest_buffer.data(), buffer_size) == 0);
      REQUIRE(
        decompressor.decompress(counts_checksum + 1U, data_checksum, compressed_buffer, dest_buffer).get() ==
        LogError::bad_checksum);
      REQUIRE(
        decompressor.decompress(counts_checksum, data_checksum + 1U, compressed_buffer, dest_buffer).get() ==
        LogError::bad_checksum);
    }

    SECTION("Decompress into internal buffer")
    {
      const auto decompress_result = decompressor.decompress(compressed_spans);
      REQUIRE(decompress_result);
      REQUIRE(decompress_result->size() == misaligned_buffer.size());
      REQUIRE(std::memcmp(misaligned_buffer.data(), decompress_result->data(), misaligned_buffer.size()) == 0);
    }

    SECTION("Zero copy decompress")
    {
      const auto decompress_result = decompressor.zero_copy_decompress(compressed_spans);
      REQUIRE(decompress_result);
      REQUIRE(spans_size(decompress_result.value()) == misaligned_buffer.size());
      REQUIRE(std::ranges::equal(decompress_result.value() | std::views::join, misaligned_buffer));
    }
  }

  SECTION("All ones")
  {
    constexpr auto buffer_size = 128U;
    alignas(sizeof(uint64_t)) std::array<std::byte, buffer_size> buffer{};
    std::memset(buffer.data(), 1, buffer_size);
    const auto compressed_spans = compressor.compress({buffer});
    REQUIRE(compressed_spans.size() == 2U);
    REQUIRE(spans_size(compressed_spans) == buffer_size + (3U * sizeof(uint32_t)));
    REQUIRE(LiteCompressor::get_decompressed_size(compressed_spans) == buffer_size);

    SECTION("With checksum")
    {
      std::span<const std::span<const std::byte>> checksum_spans;
      uint64_t counts_checksum{};
      uint64_t data_checksum{};
      compressor.compress(
        jewels::Out{checksum_spans}, jewels::Out{counts_checksum}, jewels::Out{data_checksum}, {buffer});
      const auto checksum_view = checksum_spans | std::views::join;
      REQUIRE(std::ranges::equal(compressed_spans | std::views::join, checksum_view));
      REQUIRE(counts_checksum == compute_xxh3_checksum(compressed_spans.first(1U)));
      REQUIRE(data_checksum == compute_xxh3_checksum(compressed_spans.subspan(1U)));
      std::vector<std::byte> compressed_buffer(checksum_view.begin(), checksum_view.end());
      std::vector<std::byte> dest_buffer(buffer.size());
      const auto decompress_outcome =
        decompressor.decompress(counts_checksum, data_checksum, compressed_buffer, dest_buffer);
      REQUIRE(decompress_outcome.get() == LogError::success);
      REQUIRE(std::memcmp(buffer.data(), dest_buffer.data(), buffer_size) == 0);
      REQUIRE(
        decompressor.decompress(counts_checksum + 1U, data_checksum, compressed_buffer, dest_buffer).get() ==
        LogError::bad_checksum);
      REQUIRE(
        decompressor.decompress(counts_checksum, data_checksum + 1U, compressed_buffer, dest_buffer).get() ==
        LogError::bad_checksum);
    }

    SECTION("Decompress into internal buffer")
    {
      const auto decompress_result = decompressor.decompress(compressed_spans);
      REQUIRE(decompress_result);
      REQUIRE(decompress_result->size() == buffer_size);
      REQUIRE(std::memcmp(buffer.data(), decompress_result->data(), buffer_size) == 0);
    }

    SECTION("Zero copy decompress")
    {
      const auto decompress_result = decompressor.zero_copy_decompress(compressed_spans);
      REQUIRE(decompress_result);
      REQUIRE(spans_size(decompress_result.value()) == buffer.size());
      REQUIRE(std::memcmp(buffer.data(), decompress_result->front().data(), buffer_size) == 0);
    }
  }

  SECTION("All ones misaligned")
  {
    constexpr auto buffer_size = 128U;
    alignas(sizeof(uint64_t)) std::array<std::byte, buffer_size> buffer{};
    std::memset(buffer.data(), 1, buffer_size);
    const std::span misaligned_buffer{&buffer.at(1U), buffer_size - 2U};
    const auto compressed_spans = compressor.compress(misaligned_buffer);
    REQUIRE(compressed_spans.size() == 4U);
    REQUIRE(spans_size(compressed_spans) == misaligned_buffer.size() + LiteCompressor::max_compression_overhead_bytes);
    REQUIRE(LiteCompressor::get_decompressed_size(compressed_spans) == misaligned_buffer.size());

    SECTION("With checksum")
    {
      std::span<const std::span<const std::byte>> checksum_spans;
      uint64_t counts_checksum{};
      uint64_t data_checksum{};
      compressor.compress(
        jewels::Out{checksum_spans}, jewels::Out{counts_checksum}, jewels::Out{data_checksum}, {misaligned_buffer});
      const auto checksum_view = checksum_spans | std::views::join;
      REQUIRE(std::ranges::equal(compressed_spans | std::views::join, checksum_view));
      REQUIRE(counts_checksum == compute_xxh3_checksum(compressed_spans.first(1U)));
      REQUIRE(data_checksum == compute_xxh3_checksum(compressed_spans.subspan(1U)));
      std::vector<std::byte> compressed_buffer(checksum_view.begin(), checksum_view.end());
      std::vector<std::byte> dest_buffer(misaligned_buffer.size());
      const auto decompress_outcome =
        decompressor.decompress(counts_checksum, data_checksum, compressed_buffer, dest_buffer);
      REQUIRE(decompress_outcome.get() == LogError::success);
      REQUIRE(std::memcmp(buffer.data(), dest_buffer.data(), buffer_size) == 0);
      REQUIRE(
        decompressor.decompress(counts_checksum + 1U, data_checksum, compressed_buffer, dest_buffer).get() ==
        LogError::bad_checksum);
      REQUIRE(
        decompressor.decompress(counts_checksum, data_checksum + 1U, compressed_buffer, dest_buffer).get() ==
        LogError::bad_checksum);
    }

    SECTION("Decompress into internal buffer")
    {
      const auto decompress_result = decompressor.decompress(compressed_spans);
      REQUIRE(decompress_result);
      REQUIRE(decompress_result->size() == misaligned_buffer.size());
      REQUIRE(std::memcmp(misaligned_buffer.data(), decompress_result->data(), misaligned_buffer.size()) == 0);
    }

    SECTION("Zero copy decompress")
    {
      const auto decompress_result = decompressor.zero_copy_decompress(compressed_spans);
      REQUIRE(decompress_result);
      REQUIRE(spans_size(decompress_result.value()) == misaligned_buffer.size());
      REQUIRE(std::ranges::equal(decompress_result.value() | std::views::join, misaligned_buffer));
    }
  }

  SECTION("First half ones")
  {
    constexpr auto buffer_size = 128U;
    alignas(sizeof(uint64_t)) std::array<std::byte, buffer_size> buffer{};
    std::memset(buffer.data(), 1, buffer_size / 2U);
    const auto compressed_spans = compressor.compress({buffer});
    REQUIRE(compressed_spans.size() == 2U);
    REQUIRE(spans_size(compressed_spans) == (buffer_size / 2U) + 16U);
    REQUIRE(LiteCompressor::get_decompressed_size(compressed_spans) == buffer_size);

    SECTION("With checksum")
    {
      std::span<const std::span<const std::byte>> checksum_spans;
      uint64_t counts_checksum{};
      uint64_t data_checksum{};
      compressor.compress(
        jewels::Out{checksum_spans}, jewels::Out{counts_checksum}, jewels::Out{data_checksum}, {buffer});
      const auto checksum_view = checksum_spans | std::views::join;
      REQUIRE(std::ranges::equal(compressed_spans | std::views::join, checksum_view));
      REQUIRE(counts_checksum == compute_xxh3_checksum(compressed_spans.first(1U)));
      REQUIRE(data_checksum == compute_xxh3_checksum(compressed_spans.subspan(1U)));
      std::vector<std::byte> compressed_buffer(checksum_view.begin(), checksum_view.end());
      std::vector<std::byte> dest_buffer(buffer.size());
      const auto decompress_outcome =
        decompressor.decompress(counts_checksum, data_checksum, compressed_buffer, dest_buffer);
      REQUIRE(decompress_outcome.get() == LogError::success);
      REQUIRE(std::memcmp(buffer.data(), dest_buffer.data(), buffer_size) == 0);
      REQUIRE(
        decompressor.decompress(counts_checksum + 1U, data_checksum, compressed_buffer, dest_buffer).get() ==
        LogError::bad_checksum);
      REQUIRE(
        decompressor.decompress(counts_checksum, data_checksum + 1U, compressed_buffer, dest_buffer).get() ==
        LogError::bad_checksum);
    }

    SECTION("Decompress into internal buffer")
    {
      const auto decompress_result = decompressor.decompress(compressed_spans);
      REQUIRE(decompress_result);
      REQUIRE(decompress_result->size() == buffer_size);
      REQUIRE(std::memcmp(buffer.data(), decompress_result->data(), buffer_size) == 0);
    }

    SECTION("Zero copy decompress")
    {
      const auto decompress_result = decompressor.zero_copy_decompress(compressed_spans);
      REQUIRE(decompress_result);
      REQUIRE(spans_size(decompress_result.value()) == buffer.size());
      REQUIRE(std::ranges::equal(decompress_result.value() | std::views::join, buffer));
    }
  }

  SECTION("Zero chunk to small to compress")
  {
    constexpr auto buffer_size = 128U;
    alignas(sizeof(uint64_t)) std::array<std::byte, buffer_size> buffer{};
    std::memset(&buffer.at(15U), 1, buffer_size - 30U);
    const auto compressed_spans = compressor.compress({buffer});
    REQUIRE(compressed_spans.size() == 2U);
    REQUIRE(spans_size(compressed_spans) == buffer_size + (3U * sizeof(uint32_t)));
    REQUIRE(LiteCompressor::get_decompressed_size(compressed_spans) == buffer_size);

    SECTION("With checksum")
    {
      std::span<const std::span<const std::byte>> checksum_spans;
      uint64_t counts_checksum{};
      uint64_t data_checksum{};
      compressor.compress(
        jewels::Out{checksum_spans}, jewels::Out{counts_checksum}, jewels::Out{data_checksum}, {buffer});
      const auto checksum_view = checksum_spans | std::views::join;
      REQUIRE(std::ranges::equal(compressed_spans | std::views::join, checksum_view));
      REQUIRE(counts_checksum == compute_xxh3_checksum(compressed_spans.first(1U)));
      REQUIRE(data_checksum == compute_xxh3_checksum(compressed_spans.subspan(1U)));
      std::vector<std::byte> compressed_buffer(checksum_view.begin(), checksum_view.end());
      std::vector<std::byte> dest_buffer(buffer.size());
      const auto decompress_outcome =
        decompressor.decompress(counts_checksum, data_checksum, compressed_buffer, dest_buffer);
      REQUIRE(decompress_outcome.get() == LogError::success);
      REQUIRE(std::memcmp(buffer.data(), dest_buffer.data(), buffer_size) == 0);
      REQUIRE(
        decompressor.decompress(counts_checksum + 1U, data_checksum, compressed_buffer, dest_buffer).get() ==
        LogError::bad_checksum);
      REQUIRE(
        decompressor.decompress(counts_checksum, data_checksum + 1U, compressed_buffer, dest_buffer).get() ==
        LogError::bad_checksum);
    }

    SECTION("Decompress into internal buffer")
    {
      const auto decompress_result = decompressor.decompress(compressed_spans);
      REQUIRE(decompress_result);
      REQUIRE(decompress_result->size() == buffer_size);
      REQUIRE(std::memcmp(buffer.data(), decompress_result->data(), buffer_size) == 0);
    }

    SECTION("Zero copy decompress")
    {
      const auto decompress_result = decompressor.zero_copy_decompress(compressed_spans);
      REQUIRE(decompress_result);
      REQUIRE(spans_size(decompress_result.value()) == buffer.size());
      REQUIRE(std::ranges::equal(decompress_result.value() | std::views::join, buffer));
    }
  }

  SECTION("Second half ones")
  {
    constexpr auto buffer_size = 128U;
    alignas(sizeof(uint64_t)) std::array<std::byte, buffer_size> buffer{};
    std::memset(&buffer.at(buffer_size / 2U), 1, buffer_size / 2U);
    const auto compressed_spans = compressor.compress({buffer});
    REQUIRE(compressed_spans.size() == 2U);
    REQUIRE(spans_size(compressed_spans) == (buffer_size / 2U) + 16U);
    REQUIRE(LiteCompressor::get_decompressed_size(compressed_spans) == buffer_size);

    SECTION("With checksum")
    {
      std::span<const std::span<const std::byte>> checksum_spans;
      uint64_t counts_checksum{};
      uint64_t data_checksum{};
      compressor.compress(
        jewels::Out{checksum_spans}, jewels::Out{counts_checksum}, jewels::Out{data_checksum}, {buffer});
      const auto checksum_view = checksum_spans | std::views::join;
      REQUIRE(std::ranges::equal(compressed_spans | std::views::join, checksum_view));
      REQUIRE(counts_checksum == compute_xxh3_checksum(compressed_spans.first(1U)));
      REQUIRE(data_checksum == compute_xxh3_checksum(compressed_spans.subspan(1U)));
      std::vector<std::byte> compressed_buffer(checksum_view.begin(), checksum_view.end());
      std::vector<std::byte> dest_buffer(buffer.size());
      const auto decompress_outcome =
        decompressor.decompress(counts_checksum, data_checksum, compressed_buffer, dest_buffer);
      REQUIRE(decompress_outcome.get() == LogError::success);
      REQUIRE(std::memcmp(buffer.data(), dest_buffer.data(), buffer_size) == 0);
      REQUIRE(
        decompressor.decompress(counts_checksum + 1U, data_checksum, compressed_buffer, dest_buffer).get() ==
        LogError::bad_checksum);
      REQUIRE(
        decompressor.decompress(counts_checksum, data_checksum + 1U, compressed_buffer, dest_buffer).get() ==
        LogError::bad_checksum);
    }

    SECTION("Decompress into internal buffer")
    {
      const auto decompress_result = decompressor.decompress(compressed_spans);
      REQUIRE(decompress_result);
      REQUIRE(decompress_result->size() == buffer_size);
      REQUIRE(std::memcmp(buffer.data(), decompress_result->data(), buffer_size) == 0);
    }

    SECTION("Zero copy decompress")
    {
      const auto decompress_result = decompressor.zero_copy_decompress(compressed_spans);
      REQUIRE(decompress_result);
      REQUIRE(spans_size(decompress_result.value()) == buffer.size());
      REQUIRE(std::ranges::equal(decompress_result.value() | std::views::join, buffer));
    }
  }

  SECTION("No byte counts")
  {
    REQUIRE(
      decompressor.decompress(std::span<const std::byte>{}) == jewels::unexpected(LogError::decompression_failure));
    REQUIRE(
      decompressor.zero_copy_decompress(std::span<const std::byte>{}) ==
      jewels::unexpected(LogError::decompression_failure));
    REQUIRE(
      LiteCompressor::get_decompressed_size(std::span<const std::byte>{}) ==
      jewels::unexpected(LogError::decompression_failure));
  }

  SECTION("Message size with no counts")
  {
    std::array<int32_t, 2U> byte_counts{8, 10};
    REQUIRE(
      decompressor.decompress(std::as_bytes(std::span{byte_counts})) ==
      jewels::unexpected(LogError::decompression_failure));
    REQUIRE(
      decompressor.zero_copy_decompress(std::as_bytes(std::span{byte_counts})) ==
      jewels::unexpected(LogError::decompression_failure));
  }

  SECTION("Less data than expected")
  {
    std::array<int32_t, 4U> byte_counts{12, 100, 100, 0};
    REQUIRE(
      decompressor.decompress(std::as_bytes(std::span{byte_counts})) ==
      jewels::unexpected(LogError::decompression_failure));
    REQUIRE(
      decompressor.zero_copy_decompress(std::as_bytes(std::span{byte_counts})) ==
      jewels::unexpected(LogError::decompression_failure));
  }

  SECTION("More data than expected")
  {
    std::array<int32_t, 5U> byte_counts{12, 4, 4, 0, 0};
    REQUIRE(
      decompressor.decompress(std::as_bytes(std::span{byte_counts})) ==
      jewels::unexpected(LogError::decompression_failure));
    REQUIRE(
      decompressor.zero_copy_decompress(std::as_bytes(std::span{byte_counts})) ==
      jewels::unexpected(LogError::decompression_failure));
  }

  SECTION("Less zeros than expected")
  {
    std::array<int32_t, 3U> byte_counts{12, 100, -50};
    REQUIRE(
      decompressor.decompress(std::as_bytes(std::span{byte_counts})) ==
      jewels::unexpected(LogError::decompression_failure));
    REQUIRE(
      decompressor.zero_copy_decompress(std::as_bytes(std::span{byte_counts})) ==
      jewels::unexpected(LogError::decompression_failure));
  }

  SECTION("More zeros than expected")
  {
    std::array<int32_t, 3U> byte_counts{12, 100, -150};
    REQUIRE(
      decompressor.decompress(std::as_bytes(std::span{byte_counts})) ==
      jewels::unexpected(LogError::decompression_failure));
    REQUIRE(
      decompressor.zero_copy_decompress(std::as_bytes(std::span{byte_counts})) ==
      jewels::unexpected(LogError::decompression_failure));
  }
}

} // namespace
} // namespace clockwork_logging
