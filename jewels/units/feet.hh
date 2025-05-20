// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/units/quantity.hh"

#include <au/units/feet.hh> // IWYU pragma: export

namespace au
{
using FeetF = au::QuantityF<au::Feet>;
using FeetD = au::QuantityD<au::Feet>;

/// Create a feet quantity from an integer literal.
[[nodiscard]] constexpr au::QuantityI64<au::Feet>
// NOLINTNEXTLINE(google-runtime-int) - UDL definitions require non-fixed width integers
operator""_feet(unsigned long long int value) noexcept;
} // namespace au
