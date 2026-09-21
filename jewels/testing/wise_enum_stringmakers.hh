// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

/// Converters for Catch2, so that wise_enum based enumerations print out cleanly.
#pragma once

#include <catch2/catch_tostring.hpp>
#include <wise_enum.h>

#include <string>

namespace Catch
{
/// Concept to check if a type is a wise_enum.
template <typename T>
concept WiseEnum = wise_enum::is_wise_enum_v<T>;

/// Stringmaker that converts a wise_enum value to a string.
template <WiseEnum EnumType>
struct StringMaker<EnumType>
{
  static std::string convert(const EnumType& value);
};
} // namespace Catch
#include "jewels/testing/wise_enum_stringmakers.inl"
