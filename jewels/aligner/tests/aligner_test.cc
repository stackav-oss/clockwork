// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/aligner/aligner.hh"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <iterator>

namespace jewels
{
namespace
{

constexpr size_t alignment = 32U;

TEST_CASE("align int32_t")
{
  SECTION("Negative aligned offset")
  {
    static constexpr int32_t offset = -64;

    REQUIRE(Aligner<alignment>::is_aligned(offset));
    REQUIRE(Aligner<alignment>::align_prev(offset) == offset);
    REQUIRE(Aligner<alignment>::align_next(offset) == offset);
    REQUIRE(Aligner<alignment>::aligned_offset(offset) == 0);
    REQUIRE(Aligner<alignment>::aligned_remainder(offset) == 0);
  }

  SECTION("Negative non-aligned offset")
  {
    static constexpr int32_t offset = -75;
    static constexpr int32_t expected_align_prev = -96;
    static constexpr int32_t expected_align_next = -64;
    static constexpr int32_t expected_aligned_offset = 21;
    static constexpr int32_t expected_aligned_remainder = 11;

    REQUIRE_FALSE(Aligner<alignment>::is_aligned(offset));
    REQUIRE(Aligner<alignment>::align_prev(offset) == expected_align_prev);
    REQUIRE(Aligner<alignment>::align_next(offset) == expected_align_next);
    REQUIRE(Aligner<alignment>::aligned_offset(offset) == expected_aligned_offset);
    REQUIRE(Aligner<alignment>::aligned_remainder(offset) == expected_aligned_remainder);
  }

  SECTION("Zero offset")
  {
    static constexpr int32_t offset = 0;

    REQUIRE(Aligner<alignment>::is_aligned(offset));
    REQUIRE(Aligner<alignment>::align_prev(offset) == offset);
    REQUIRE(Aligner<alignment>::align_next(offset) == offset);
    REQUIRE(Aligner<alignment>::aligned_offset(offset) == 0);
    REQUIRE(Aligner<alignment>::aligned_remainder(offset) == 0);
  }

  SECTION("Positive aligned offset")
  {
    static constexpr int32_t offset = 64;

    REQUIRE(Aligner<alignment>::is_aligned(offset));
    REQUIRE(Aligner<alignment>::align_prev(offset) == offset);
    REQUIRE(Aligner<alignment>::align_next(offset) == offset);
    REQUIRE(Aligner<alignment>::aligned_offset(offset) == 0);
    REQUIRE(Aligner<alignment>::aligned_remainder(offset) == 0);
  }

  SECTION("Positive non-aligned offset")
  {
    static constexpr int32_t offset = 75;
    static constexpr int32_t expected_align_prev = 64;
    static constexpr int32_t expected_align_next = 96;
    static constexpr int32_t expected_aligned_offset = 11;
    static constexpr int32_t expected_aligned_remainder = 21;

    REQUIRE_FALSE(Aligner<alignment>::is_aligned(offset));
    REQUIRE(Aligner<alignment>::align_prev(offset) == expected_align_prev);
    REQUIRE(Aligner<alignment>::align_next(offset) == expected_align_next);
    REQUIRE(Aligner<alignment>::aligned_offset(offset) == expected_aligned_offset);
    REQUIRE(Aligner<alignment>::aligned_remainder(offset) == expected_aligned_remainder);
  }
}

TEST_CASE("align size_t")
{
  SECTION("Zero offset")
  {
    static constexpr size_t offset = 0U;

    REQUIRE(Aligner<alignment>::is_aligned(offset));
    REQUIRE(Aligner<alignment>::align_prev(offset) == offset);
    REQUIRE(Aligner<alignment>::align_next(offset) == offset);
    REQUIRE(Aligner<alignment>::aligned_offset(offset) == 0U);
    REQUIRE(Aligner<alignment>::aligned_remainder(offset) == 0U);
  }

  SECTION("Aligned offset")
  {
    static constexpr size_t offset = 64U;

    REQUIRE(Aligner<alignment>::is_aligned(offset));
    REQUIRE(Aligner<alignment>::align_prev(offset) == offset);
    REQUIRE(Aligner<alignment>::align_next(offset) == offset);
    REQUIRE(Aligner<alignment>::aligned_offset(offset) == 0U);
    REQUIRE(Aligner<alignment>::aligned_remainder(offset) == 0U);
  }

  SECTION("Non-aligned offset")
  {
    static constexpr size_t offset = 75U;
    static constexpr size_t expected_align_prev = 64U;
    static constexpr size_t expected_align_next = 96U;
    static constexpr size_t expected_aligned_offset = 11U;
    static constexpr size_t expected_aligned_remainder = 21U;

    REQUIRE_FALSE(Aligner<alignment>::is_aligned(offset));
    REQUIRE(Aligner<alignment>::align_prev(offset) == expected_align_prev);
    REQUIRE(Aligner<alignment>::align_next(offset) == expected_align_next);
    REQUIRE(Aligner<alignment>::aligned_offset(offset) == expected_aligned_offset);
    REQUIRE(Aligner<alignment>::aligned_remainder(offset) == expected_aligned_remainder);
  }

  SECTION("null pointer")
  {
    static constexpr const char* pointer = nullptr;

    REQUIRE(Aligner<alignment>::ptr_is_aligned(pointer));
    REQUIRE(Aligner<alignment>::ptr_aligned_offset(pointer) == 0U);
    REQUIRE(Aligner<alignment>::ptr_aligned_remainder(pointer) == 0U);
  }

  SECTION("Aligned pointer")
  {
    alignas(64) const uint8_t test_value{};
    const auto* pointer = &test_value;

    REQUIRE(Aligner<alignment>::ptr_is_aligned(pointer));
    REQUIRE(Aligner<alignment>::ptr_aligned_offset(pointer) == 0U);
    REQUIRE(Aligner<alignment>::ptr_aligned_remainder(pointer) == 0U);
  }

  SECTION("Non-aligned pointer")
  {
    constexpr auto aligned_offset{11U};
    alignas(64) const std::array<std::byte, aligned_offset + 1U> bytes{};
    const auto* pointer = std::prev(std::end(bytes));
    static constexpr size_t expected_aligned_offset = aligned_offset;
    static constexpr size_t expected_aligned_remainder = 21U;

    REQUIRE_FALSE(Aligner<alignment>::ptr_is_aligned(pointer));
    REQUIRE(Aligner<alignment>::ptr_aligned_offset(pointer) == expected_aligned_offset);
    REQUIRE(Aligner<alignment>::ptr_aligned_remainder(pointer) == expected_aligned_remainder);
  }
}

} // namespace
} // namespace jewels
