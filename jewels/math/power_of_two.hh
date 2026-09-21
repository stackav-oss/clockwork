// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>

namespace jewels::math
{

/// Check if a value is a power of two.
/// @param power_of_two The value to check.
/// @return True if a power of two and false otherwise.
template <class UnsignedInteger>
constexpr bool is_power_of_two(UnsignedInteger power_of_two);

/// Round a value up to the nearest multiple of a power of two if it isn't already.
/// @tparam power_of_two The power of two.
/// @param value_to_round The value to round up.
/// @return The rounded value.
template <size_t power_of_two, class UnsignedInteger>
constexpr UnsignedInteger round_up_to_power_of_two_multiple(UnsignedInteger value_to_round);

/// Round a value up to the nearest power of two if it isn't already.
/// @param value_to_round The value to round up.
/// @return The rounded value.
constexpr size_t round_up_to_power_of_two(size_t value_to_round);

} // namespace jewels::math

#include "jewels/math/power_of_two.inl"
