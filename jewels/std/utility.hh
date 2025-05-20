// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <type_traits>

namespace jewels
{

// Backport of https://en.cppreference.com/w/cpp/utility/to_underlying
template <typename Enum>
constexpr std::underlying_type_t<Enum> to_underlying(Enum value) noexcept
{
  return static_cast<std::underlying_type_t<Enum>>(value);
}

} // namespace jewels
