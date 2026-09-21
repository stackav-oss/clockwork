// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/testing/wise_enum_stringmakers.hh" // IWYU pragma: keep

#include <catch2/catch_test_macros.hpp>
#include <wise_enum.h>

#include <cstdint>

namespace Catch
{
/// Test enumeration for ensuring conversion works as expected.
WISE_ENUM_CLASS((Color, int8_t), red, green, blue)

TEST_CASE("StringMaker logic")
{
  CHECK("red" == StringMaker<Color>::convert(Color::red));
  CHECK("green" == StringMaker<Color>::convert(Color::green));
  CHECK("blue" == StringMaker<Color>::convert(Color::blue));
}

TEST_CASE("StringMaker registers with Catch2")
{
  // This test is added because we use a concept to specialize the struct, which does provide a more specialized
  // template. However this can be a bit surprising, so we test that Catch2 registers the specialization correctly here.
  CHECK(::Catch::Detail::stringify(Color::red) == "red");
}
} // namespace Catch
