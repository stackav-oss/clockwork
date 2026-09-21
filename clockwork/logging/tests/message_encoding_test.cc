// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/cpp_message_encoding.hh"
#include "clockwork/logging/message_encoding_clk_cc.hh"
#include "jewels/testing/wise_enum_sync.hh"

#include <catch2/catch_test_macros.hpp>

namespace clockwork_logging
{
namespace
{

TEST_CASE("CppMessageEncoding <-> MessageEncoding")
{
  REQUIRE_WISE_ENUM_SYNC(CppMessageEncoding, MessageEncoding);
}

} // namespace
} // namespace clockwork_logging
