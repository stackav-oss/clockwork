// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dsl/cog/ten_nanosecond_type.hh"

#include <catch2/catch_test_macros.hpp>
namespace clockwork
{
TEST_CASE("Validate 10 ns units")
{
  REQUIRE(std::chrono::duration_cast<std::chrono::nanoseconds>(TenNanoseconds{123}) == std::chrono::nanoseconds{1230});
  // Test the factory function
  auto ten_ns = ten_nanoseconds_factory(100);
  CHECK(ten_ns.count() == 100);
}
} // namespace clockwork
