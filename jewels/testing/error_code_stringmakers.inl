// IWYU pragma: private, include "jewels/testing/error_code_stringmakers.hh"
#pragma once

#include "jewels/testing/error_code_stringmakers.hh"

#include "jewels/filesystem/error_code.hh"

#include <catch2/catch_tostring.hpp>

#include <string>
#include <system_error>

namespace Catch
{
std::string StringMaker<jewels::filesystem::ErrorCode>::convert(const jewels::filesystem::ErrorCode& value)
{
  return value.message();
}

std::string StringMaker<std::errc>::convert(const std::errc& value)
{
  return std::make_error_code(value).message();
}
} // namespace Catch
