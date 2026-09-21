// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

/// Converters for Catch2, so that filesystem types print out cleanly.
#pragma once

#include "jewels/filesystem/path.hh"

#include <catch2/catch_tostring.hpp>

#include <string>

namespace Catch
{
template <>
struct StringMaker<jewels::filesystem::Path>
{
  static std::string convert(const jewels::filesystem::Path& maybe_value);
};
} // namespace Catch
#include "jewels/testing/path_stringmakers.inl"
