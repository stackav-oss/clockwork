// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/meta/overloaded.hh"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <utility>
#include <variant>

namespace jewels::meta::test
{

struct A
{
};

struct B
{
};

struct C
{
};

using VariantType = std::variant<std::monostate, A, B, C>;

constexpr inline auto monostate_value = 2U;
constexpr inline auto a_value = 4U;
constexpr inline auto b_value = 6U;
constexpr inline auto c_value = 8U;

[[nodiscard]] uint32_t do_visit(const VariantType& arg)
{
  return std::visit(
    Overloaded{
      // Handle std::monostate
      [](std::monostate) { return monostate_value; },
      // Handle A
      [](A) { return a_value; },
      // Handle B
      [](B) { return b_value; },
      // Handle C
      [](C) { return c_value; }},
    arg);
}

TEST_CASE("Overloaded")
{
  SECTION("Monostate")
  {
    auto mono_var = VariantType{};
    REQUIRE(do_visit(mono_var) == monostate_value);
  }

  SECTION("A")
  {
    auto a_var = VariantType{std::in_place_type<A>};
    REQUIRE(do_visit(a_var) == a_value);
  }

  SECTION("B")
  {
    auto b_var = VariantType{std::in_place_type<B>};
    REQUIRE(do_visit(b_var) == b_value);
  }

  SECTION("C")
  {
    auto c_var = VariantType{std::in_place_type<C>};
    REQUIRE(do_visit(c_var) == c_value);
  }
}

} // namespace jewels::meta::test
