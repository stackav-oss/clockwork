// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/std/utility.hh"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <type_traits>

namespace
{

enum class TestEnum16 : uint16_t
{
  value_123 = 123,
  value_5k = 5000,
};

enum class TestEnum32 : uint32_t
{
  value_123 = 123,
  value_1m = 1000000,
};

TEST_CASE("to_underlying test")
{
  static_assert(!std::is_same_v<decltype(TestEnum16::value_123), uint16_t>);
  static_assert(!std::is_same_v<decltype(TestEnum32::value_123), uint32_t>);
  static_assert(std::is_same_v<decltype(jewels::to_underlying(TestEnum16::value_123)), uint16_t>);
  static_assert(std::is_same_v<decltype(jewels::to_underlying(TestEnum32::value_123)), uint32_t>);
  CHECK(jewels::to_underlying(TestEnum16::value_123) == 123);
  CHECK(jewels::to_underlying(TestEnum16::value_5k) == 5000);
  CHECK(jewels::to_underlying(TestEnum32::value_123) == 123);
  CHECK(jewels::to_underlying(TestEnum32::value_1m) == 1000000);
}

} // namespace
