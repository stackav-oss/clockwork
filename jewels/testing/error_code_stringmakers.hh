// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

/// Converters for Catch2, so that filesystem types print out cleanly.
#pragma once

#include "jewels/filesystem/error_code.hh"

#include <catch2/catch_tostring.hpp>

#include <string>
#include <system_error>

namespace Catch
{
template <>
struct StringMaker<jewels::filesystem::ErrorCode>
{
  static std::string convert(const jewels::filesystem::ErrorCode& value);
};

template <>
struct StringMaker<std::errc>
{
  static std::string convert(const std::errc& value);
};
} // namespace Catch
#include "jewels/testing/error_code_stringmakers.inl"
