// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/units/quantity.hh"

#include <au/units/pounds_mass.hh> // IWYU pragma: export

namespace au
{
using PoundsMassF = au::QuantityF<au::PoundsMass>;
using PoundsMassD = au::QuantityD<au::PoundsMass>;

/// Create a pounds mass quantity from an integer literal.
[[nodiscard]] constexpr au::QuantityI64<au::PoundsMass>
// NOLINTNEXTLINE(google-runtime-int) - UDL definitions require non-fixed width integers
operator""_pounds_mass(unsigned long long int value) noexcept;
} // namespace au

#include "jewels/units/pounds_mass.inl"
