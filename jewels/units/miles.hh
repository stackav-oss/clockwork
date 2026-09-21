// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/units/quantity.hh"

#include <au/units/miles.hh> // IWYU pragma: export

namespace au
{
/// Create a mile quantity from an integer literal.
[[nodiscard]] constexpr au::QuantityI64<au::Miles>
// NOLINTNEXTLINE(google-runtime-int) - UDL definitions require non-fixed width integers
operator""_miles(unsigned long long int value) noexcept;
} // namespace au

#include "jewels/units/miles.inl"
