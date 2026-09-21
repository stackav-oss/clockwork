// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/filesystem/error_code.hh"
#include "jewels/testing/error_code_stringmakers.hh" // IWYU pragma: keep

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_tostring.hpp>

#include <cerrno>
#include <string>
#include <system_error>

namespace Catch
{
TEST_CASE("ErrorCode Stringmakers")
{
  const auto error_code = jewels::filesystem::make_error_code(ENOENT);
  CHECK("No such file or directory" == StringMaker<jewels::filesystem::ErrorCode>::convert(error_code));
}
TEST_CASE("std::errc Stringmakers")
{
  const auto error_code = std::errc::bad_address;
  CHECK("Bad address" == StringMaker<std::errc>::convert(error_code));
}
} // namespace Catch
