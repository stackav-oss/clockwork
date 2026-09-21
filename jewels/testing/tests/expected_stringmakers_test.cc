// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/testing/expected_stringmakers.hh" // IWYU pragma: keep

#include <catch2/catch_test_macros.hpp>

namespace Catch
{
TEST_CASE("jewels::expected string maker")
{
  using ExpectedInt = jewels::expected<int, std::string>;

  CHECK("42" == StringMaker<ExpectedInt>::convert(ExpectedInt(42)));
  CHECK("\"error\"" == StringMaker<ExpectedInt>::convert(jewels::unexpected(std::string("error"))));
}

TEST_CASE("jewels::expected with void return type")
{
  using ExpectedVoid = jewels::expected<void, std::string>;

  CHECK("void" == StringMaker<ExpectedVoid>::convert(ExpectedVoid{}));
  CHECK("\"error\"" == StringMaker<ExpectedVoid>::convert(jewels::unexpected(std::string("error"))));
}

TEST_CASE("jewels::MonoError string maker")
{
  CHECK("MonoError" == StringMaker<jewels::MonoError>::convert(jewels::MonoError{}));
}
} // namespace Catch
