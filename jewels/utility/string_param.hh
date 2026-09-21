// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace jewels
{

/// Helper class to pass strings as template parameters
/// This class follows a C++20 pattern for passing strings as template parameters.
/// You can define a template with a string parameter as
///
///   template <StringParam str>
///   class MyClass { ... };
///
/// And then you can pass strings as template parameters with `MyClass<"Testing123">`.
///
/// And you can use the value inside the class like this:
///
///   static constexpr std::string_view str_param = str;
///
/// The implememtation uses an implicit constructor for usability.
/// The use of C-arrays is what makes the pattern work.
///
/// @tparam string_size String size in bytes
template <std::size_t string_size>
struct StringParam
{
  /// Constructor
  /// @param str Character array terminated by '\0' (aka string literal).
  // NOLINTNEXTLINE(modernize-avoid-c-arrays, google-explicit-constructor) See class description.
  constexpr StringParam(const char (&str)[string_size]);

  /// Implicit string_view operator
  // NOLINTNEXTLINE(google-explicit-constructor) Intentionally implicit
  constexpr operator std::string_view() const;

  constexpr bool operator==(const StringParam&) const = default;
  constexpr auto operator<=>(const StringParam&) const = default;

  /// String storage
  std::array<char, string_size> data;
};

/// Deduction guide
template <std::size_t string_size>
// NOLINTNEXTLINE(modernize-avoid-c-arrays) C-array is required for pattern to work
StringParam(const char (&str)[string_size]) -> StringParam<string_size>;

} // namespace jewels

#include "jewels/utility/string_param.inl"
