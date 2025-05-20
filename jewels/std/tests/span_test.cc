// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/std/span.hh"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>
#include <span>

namespace
{

TEST_CASE("Smoke test")
{
  std::array arr = {1, 2, 3};
  const std::span<const int> span{arr};
  REQUIRE(span.size() == 3);
  REQUIRE(span.data() == arr.data());
  REQUIRE(std::to_address(span.begin()) == arr.data());
  REQUIRE(std::to_address(span.end()) == arr.data() + 3);
}

TEST_CASE("Single span")
{
  int value{7U};
  const auto span = jewels::as_single_item_span(value);
  REQUIRE(span.size() == 1);
  REQUIRE(span.data() == &value);
  REQUIRE(span[0U] == 7);
}

} // namespace
