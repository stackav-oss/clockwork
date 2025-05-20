// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/units/quantity.hh"

#include <au/au.hh>            // IWYU pragma: export
#include <au/units/minutes.hh> // IWYU pragma: export

namespace au
{
/// Create a minute quantity from an integer literal.
[[nodiscard]] constexpr au::QuantityI64<au::Minutes>
// NOLINTNEXTLINE(google-runtime-int) - UDL definitions require non-fixed width integers
operator""_minutes(unsigned long long int value) noexcept;
} // namespace au

#include "jewels/units/minutes.inl"
