// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dsl/tests/support/taptags.hh"

#include <catch2/catch_test_macros.hpp>

namespace
{

TEST_CASE("ctor", "[taptags]")
{
  REQUIRE_NOTHROW(::clockwork::testing::separate_tags::AnotherTag{});
}

} // namespace
