// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/cpp_schema_encoding.hh"
#include "clockwork/logging/schema_encoding_clk_cc.hh"
#include "jewels/testing/wise_enum_sync.hh"

#include <catch2/catch_test_macros.hpp>

namespace clockwork_logging
{
namespace
{

TEST_CASE("CppSchemaEncoding <-> SchemaEncoding")
{
  REQUIRE_WISE_ENUM_SYNC(CppSchemaEncoding, SchemaEncoding);
}

} // namespace
} // namespace clockwork_logging
