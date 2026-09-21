// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dsl/tests/support/taptags_clk_cc.hh"

#include <catch2/catch_test_macros.hpp>

namespace
{

TEST_CASE("ctor", "[taptags]")
{
  REQUIRE_NOTHROW(::clockwork::testing::separate_tags::AnotherTag{});
}

} // namespace
