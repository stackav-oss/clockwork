// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <array>
#include <cstddef>

namespace jewels
{

///
/// Returns true if all the members of the arrays are equal, or if N==0
/// NOTE: while operator== is constexpr in modern libstdc++, the version we're using doesn't support that
///
template <typename T, size_t n>
constexpr bool array_eq(const std::array<T, n>& lhs, const std::array<T, n>& rhs);

///
/// Returns true if the first non-equal member of lhs is less than rhs, or false if N==0 or the arrays are equal
/// NOTE: while operator< is constexpr in modern libstdc++, the version we're using doesn't support that
///
template <typename T, size_t n>
constexpr bool array_lt(const std::array<T, n>& lhs, const std::array<T, n>& rhs);

} // namespace jewels

#include "jewels/container/compare.inl"
