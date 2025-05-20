// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/units/quantity.hh"

#include <au/au.hh>          // IWYU pragma: export
#include <au/units/hours.hh> // IWYU pragma: export

namespace au
{
/// Create a hour quantity from an integer literal.
[[nodiscard]] constexpr au::QuantityI64<au::Hours>
// NOLINTNEXTLINE(google-runtime-int) - UDL definitions require non-fixed width integers
operator""_hours(unsigned long long int value) noexcept;
} // namespace au

#include "jewels/units/hours.inl"
