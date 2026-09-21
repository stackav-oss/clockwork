// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dsl/tests/support/hello_msg_onboard.hh"

#include <catch2/catch_test_macros.hpp>

namespace
{

TEST_CASE("ctor", "[hellomsg]")
{
  REQUIRE_NOTHROW(::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>());
}

} // namespace
