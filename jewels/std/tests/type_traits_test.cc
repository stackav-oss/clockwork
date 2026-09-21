// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/std/type_traits.hh"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

namespace
{

enum class TestEnum16 : uint16_t
{
};

enum class TestEnum32 : uint32_t
{
};

// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class) This is intentionally a classic enum for testing purposes.
enum ClassicEnum
{
};

struct Struct
{
};

TEST_CASE("to_underlying test")
{
  CHECK(jewels::is_scoped_enum_v<TestEnum16>);
  CHECK(jewels::is_scoped_enum_v<TestEnum32>);
  CHECK(!jewels::is_scoped_enum_v<ClassicEnum>);
  CHECK(!jewels::is_scoped_enum_v<Struct>);
}

} // namespace
