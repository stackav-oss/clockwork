// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/container/compare.hh"

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <utility>

namespace jewels
{
namespace
{

template <typename T>
T gen_value(size_t idx)
{
  return static_cast<T>((3 * idx) + 10);
}

template <>
std::string gen_value(size_t idx)
{
  return std::to_string(gen_value<size_t>(idx));
}

template <typename T, size_t... idx>
auto gen_array(std::index_sequence<idx...> /*indicies*/)
{
  return std::array<T, sizeof...(idx)>{{gen_value<T>(idx)...}};
}

template <typename T>
void make_less_than(T& value)
{
  REQUIRE(value - 1 < value); // Sanity check underflow
  value = value - 1;
}

template <>
void make_less_than(std::byte& value)
{
  REQUIRE(value != std::byte{0});
  value = static_cast<std::byte>(static_cast<uint8_t>(value) - 1);
}

template <>
void make_less_than(std::string& value)
{
  REQUIRE(!value.empty());
  value = value.substr(0, value.size() - 1);
}

TEMPLATE_TEST_CASE_SIG("array_cmp", "", ((typename T, size_t n), T, n), (uint8_t, 0), (std::byte, 4), (std::string, 15))
{
  auto lhs = gen_array<T>(std::make_index_sequence<n>{});
  auto rhs = gen_array<T>(std::make_index_sequence<n>{});
  CHECK(array_eq(lhs, rhs));
  CHECK(!array_lt(lhs, rhs));
  if constexpr (n > 0)
  {
    make_less_than(lhs[n - 1]);
    CHECK(!array_eq(lhs, rhs));
    CHECK(array_lt(lhs, rhs));

    make_less_than(rhs[n - 1]);
    CHECK(array_eq(lhs, rhs));
    CHECK(!array_lt(lhs, rhs));

    make_less_than(rhs[0]);
    CHECK(!array_eq(lhs, rhs));
    CHECK(!array_lt(lhs, rhs));
  }
}

TEST_CASE("constexpr array_cmp")
{
  constexpr std::array<int, 3> base{{1, 2, 3}};
  STATIC_CHECK(array_eq(base, base));
  STATIC_CHECK(!array_lt(base, base));
  constexpr std::array<int, 3> less{{1, 1, 3}};
  STATIC_CHECK(!array_eq(less, base));
  STATIC_CHECK(array_lt(less, base));
  STATIC_CHECK(!array_lt(base, less));
}
} // namespace
} // namespace jewels
