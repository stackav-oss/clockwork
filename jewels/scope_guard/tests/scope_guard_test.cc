// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/scope_guard/scope_guard.hh"

#include <catch2/catch_test_macros.hpp>

namespace jewels
{
TEST_CASE("ScopeGuard")
{
  int value = 7;

  {
    const ScopeGuard test_scope_guard([&value]() { value = 12; });

    CHECK(value == 7);
  }

  CHECK(value == 12);
}

} // namespace jewels
