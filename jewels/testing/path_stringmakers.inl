// IWYU pragma: private, include "jewels/testing/path_stringmakers.hh"
#pragma once

#include "jewels/testing/path_stringmakers.hh"

#include "jewels/filesystem/path.hh"

#include <catch2/catch_tostring.hpp>

#include <string>

namespace Catch
{
std::string StringMaker<jewels::filesystem::Path>::convert(const jewels::filesystem::Path& maybe_value)
{
  return std::string(maybe_value.string_view());
}
} // namespace Catch
