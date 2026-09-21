// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/std/atomic.hh"

#include <catch2/catch_test_macros.hpp>

namespace jewels
{
TEST_CASE("atomic_fetch_max_explicit", "[atomic]")
{
  std::atomic<int> atomic_int{0};

  REQUIRE(atomic_fetch_max_explicit(&atomic_int, 5, std::memory_order_relaxed) == 0);
  REQUIRE(atomic_int.load() == 5);

  REQUIRE(atomic_fetch_max_explicit(&atomic_int, 3, std::memory_order_relaxed) == 5);
  REQUIRE(atomic_int.load() == 5);

  REQUIRE(atomic_fetch_max_explicit(&atomic_int, 10, std::memory_order_relaxed) == 5);
  REQUIRE(atomic_int.load() == 10);
}
} // namespace jewels
