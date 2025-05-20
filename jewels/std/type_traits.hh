// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <type_traits>

namespace jewels
{

/// Backport of https://en.cppreference.com/w/cpp/types/is_scoped_enum
template <class T, typename = void>
struct is_scoped_enum : std::bool_constant<false> // NOLINT(readability-identifier-naming)
{
};

template <class T>
struct is_scoped_enum<T, std::void_t<std::underlying_type_t<T>>>
  : std::bool_constant<std::is_enum_v<T> && !std::is_convertible_v<T, std::underlying_type_t<T>>>
{
};

template <class T>
inline constexpr bool is_scoped_enum_v = is_scoped_enum<T>::value;

} // namespace jewels
