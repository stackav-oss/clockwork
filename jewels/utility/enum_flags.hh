// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/std/type_traits.hh" // IWYU pragma: keep
#include "jewels/std/utility.hh"

#include <type_traits>

namespace jewels
{

/// Enables the given enum "name" to be treated as a collection of boolean flags.  In particular, this defines a trait
/// for the given enum name that is detected by enable_if_flags_scoped_enum below to enable boolean operators on values
/// of the enum.
/// Note that this enforces the given enum is of an unsigned type (which requires use of an enum-base, e.g. `enum class
/// X : uint16_t`).  This is purely because clang-tidy currently disallows binary operations on signed integers.  The
/// static assert allows for a fast failure before working code get written only to ultimately fail during later
/// testing.
// The bugprone-macro-parentheses within is OK because this macro is never used
// in expressions.
/// NOLINTNEXTLINE(cppcoreguidelines-macro-usage) A macro cuts down on boilerplate.
#define JEWELS_ENABLE_ENUM_FLAGS(name) \
  [[maybe_unused]] inline void jewels_scoped_enum_is_flags(name /*unused*/) \
  { \
    static_assert(std::is_unsigned_v<std::underlying_type_t<name>>); /*NOLINT(bugprone-macro-parentheses)*/ \
  }

/// Alias for std::enable_if<..., Enum> where the expression is enabled if Enum is a scoped enum and a function
/// `jewels_scoped_enum_is_flags(Enum)` is defined (provided by the JEWELS_ENABLE_ENUM_FLAGS macro)
template <typename Enum, typename = std::void_t<decltype(jewels_scoped_enum_is_flags(std::declval<Enum>()))>>
using enable_if_flags_scoped_enum = std::enable_if_t<::jewels::is_scoped_enum_v<Enum>, Enum>;

} // namespace jewels

/// Returns the a union of the two flag sets, i.e. the result will a flag set if it's set in either "a" or "b"
/// E.g. Given a = 0x23, b = 0x11, then a + b = 0x33
template <typename Enum>
inline ::jewels::enable_if_flags_scoped_enum<Enum> operator|(Enum value_a, Enum value_b)
{
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
  return static_cast<Enum>(::jewels::to_underlying(value_a) | ::jewels::to_underlying(value_b));
}

/// Returns the a union of the two flag sets, i.e. the result will a flag set if it's set in either "a" or "b"
/// E.g. Given a = 0x23, b = 0x11, then a + b = 0x33
template <typename Enum>
inline ::jewels::enable_if_flags_scoped_enum<Enum> operator+(Enum value_a, Enum value_b)
{
  return value_a | value_b;
}

/// Returns the logical "and not" of two sets of flags.  The return value will contain all flags from "a" that aren't
/// present in "b".
/// E.g. Given a = 0x11, b = 0x01, then a - b = 0x10
template <typename Enum>
inline ::jewels::enable_if_flags_scoped_enum<Enum> operator-(Enum value_a, Enum value_b)
{
  return static_cast<Enum>(
    ::jewels::to_underlying(value_a) & static_cast<std::underlying_type_t<Enum>>(~::jewels::to_underlying(value_b)));
}

/// Returns the logical "xor" of two sets of flags.  The return value will contain all flags from "a" and "b" that don't
/// appear in both.
template <typename Enum>
inline ::jewels::enable_if_flags_scoped_enum<Enum> operator^(Enum value_a, Enum value_b)
{
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
  return static_cast<Enum>(::jewels::to_underlying(value_a) ^ ::jewels::to_underlying(value_b));
}

/// Returns the logical "and" of two sets of flags.  The return value will contain all flags that appear in both "a" and
/// "b".
template <typename Enum>
inline ::jewels::enable_if_flags_scoped_enum<Enum> operator&(Enum value_a, Enum value_b)
{
  return static_cast<Enum>(::jewels::to_underlying(value_a) & ::jewels::to_underlying(value_b));
}

/// Updates "a" with the union of the flags in "a" and "b" then returns a reference to "a".
template <typename Enum>
inline ::jewels::enable_if_flags_scoped_enum<Enum> operator|=(Enum& value_a, Enum value_b)
{
  return value_a = value_a | value_b;
}

/// Updates "a" with the union of the flags in "a" and "b" then returns a reference to "a".
template <typename Enum>
inline ::jewels::enable_if_flags_scoped_enum<Enum> operator+=(Enum& value_a, Enum value_b)
{
  return value_a = value_a + value_b;
}

/// Updates "a" by removing all flags set in "b" then returns a reference to "a".
template <typename Enum>
inline ::jewels::enable_if_flags_scoped_enum<Enum> operator-=(Enum& value_a, Enum value_b)
{
  return value_a = value_a - value_b;
}

/// Updates "a" to be the set of flags that are present in "a" and "b" but not both then returns a reference to "a".
template <typename Enum>
inline ::jewels::enable_if_flags_scoped_enum<Enum> operator^=(Enum& value_a, Enum value_b)
{
  return value_a = value_a ^ value_b;
}

/// Updates "a" by removing all flags not in "b" then returns a reference to "a".
template <typename Enum>
inline ::jewels::enable_if_flags_scoped_enum<Enum> operator&=(Enum& value_a, Enum value_b)
{
  return value_a = value_a & value_b;
}
