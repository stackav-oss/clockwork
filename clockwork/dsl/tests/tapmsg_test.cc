// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dsl/tests/support/tapmsg.hh"

#include <catch2/catch_test_macros.hpp>

namespace
{

TEST_CASE("ctor", "[tapmsg]")
{
  REQUIRE_NOTHROW(clockwork::Tap<clockwork::Tachyon<::clockwork::testing::SubMsg>>());
  REQUIRE_NOTHROW(clockwork::Tap<clockwork::Tachyon<::clockwork::testing::TapMsg>>());
}

} // namespace
