// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/memory/start_lifetime_as.hh"
#include "jewels/std/span.hh"

#include <catch2/catch_test_macros.hpp>

#include <span>

namespace clockwork
{

TEST_CASE("start liftime as")
{
  int value{7};
  int* ptr = start_lifetime_as<int>(as_writable_bytes(jewels::as_single_item_span(value)));
  REQUIRE(&value == ptr);
  REQUIRE(*ptr == 7);
  const int* const_ptr = start_lifetime_as<const int>(as_bytes(jewels::as_single_item_span(value)));
  REQUIRE(&value == const_ptr);
  REQUIRE(*const_ptr == 7);

  ++value;
  REQUIRE(value == 8);
  REQUIRE(*ptr == 8);
  REQUIRE(*const_ptr == 8);
}

} // namespace clockwork
