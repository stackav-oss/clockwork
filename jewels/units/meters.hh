// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/units/quantity.hh"

// IWYU pragma: no_include <au/magnitude.hh>
#include <au/power_aliases.hh>
#include <au/prefix.hh>       // IWYU pragma: export
#include <au/units/meters.hh> // IWYU pragma: export

namespace au
{
using MetersF = au::QuantityF<au::Meters>;
using MetersD = au::QuantityD<au::Meters>;

using Kilometers = au::Kilo<au::Meters>;
using KilometersF = au::QuantityF<au::Kilometers>;
using KilometersD = au::QuantityD<au::Kilometers>;
constexpr auto kilometers = au::kilo(au::meters);

using MetersSquared = decltype(au::squared(au::Meters{}));
using MetersSquaredF = au::QuantityF<au::MetersSquared>;
using MetersSquaredD = au::QuantityD<au::MetersSquared>;
constexpr auto meters_squared = au::squared(au::meters);

/// Create a meter quantity from an integer literal.
[[nodiscard]] constexpr au::QuantityI64<au::Meters>
// NOLINTNEXTLINE(google-runtime-int) - UDL definitions require non-fixed width integers
operator""_meters(unsigned long long int value) noexcept;

/// Create a meter quantity from a floating-point literal.
[[nodiscard]] constexpr au::QuantityD<au::Meters> operator""_meters(long double value) noexcept;

/// Create a meter quantity from an integer literal.
[[nodiscard]] constexpr au::QuantityI64<au::Meters>
// NOLINTNEXTLINE(google-runtime-int) - UDL definitions require non-fixed width integers
operator""_m(unsigned long long int value) noexcept;

/// Create a meter quantity from a floating-point literal.
[[nodiscard]] constexpr au::QuantityD<au::Meters> operator""_m(long double value) noexcept;
} // namespace au

#include "jewels/units/meters.inl"
