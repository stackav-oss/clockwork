// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/meta/types.hh"

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

namespace jewels::meta
{
TEST_CASE("Test size")
{
  REQUIRE(size(Types<int, bool, float>{}) == 3);
  REQUIRE(size(Types<int, int, int>{}) == 3);
}

TEST_CASE("Indexing into Types")
{
  using TestType = Types<int, bool, float>;
  CHECK(std::is_same_v<IndexedType::Type<0, TestType>, int>);
  CHECK(std::is_same_v<IndexedType::Type<1, TestType>, bool>);
  CHECK(std::is_same_v<IndexedType::Type<2, TestType>, float>);
}

TEST_CASE("Always false")
{
  STATIC_REQUIRE(!always_false_v<int>);
  STATIC_REQUIRE(!AlwaysFalse<int>{});
}

TEST_CASE("NonType")
{
  constexpr auto fn_ptr = &decltype([] {})::operator();
  STATIC_REQUIRE(NonType<fn_ptr>::value == fn_ptr);
  STATIC_REQUIRE(decltype(non_type_v<fn_ptr>)::value == fn_ptr);
}
} // namespace jewels::meta
