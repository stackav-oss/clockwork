// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/units/magnitude.hh"
#include "jewels/units/quantity.hh"

#include <au/au.hh> // IWYU pragma: export
#include <au/power_aliases.hh>
#include <au/prefix.hh>        // IWYU pragma: export
#include <au/units/seconds.hh> // IWYU pragma: export

namespace au
{
using Nanoseconds = au::Nano<Seconds>;
constexpr auto nanosecond = au::nano(second);
constexpr auto nanoseconds = au::nano(seconds);

using Milliseconds = au::Milli<Seconds>;
constexpr auto millisecond = au::milli(second);
constexpr auto milliseconds = au::milli(seconds);

using SecondsF = au::QuantityF<Seconds>;
using SecondsD = au::QuantityD<Seconds>;

using OnePerSecond = decltype(au::inverse(au::Seconds{}));
using OnePerSecondF = au::QuantityF<OnePerSecond>;
using OnePerSecondD = au::QuantityD<OnePerSecond>;
constexpr auto one_per_second = au::inverse(au::seconds);

} // namespace au

namespace au
{
/// Create a second quantity from an integer literal.
[[nodiscard]] constexpr au::QuantityI64<au::Seconds>
// NOLINTNEXTLINE(google-runtime-int) - UDL definitions require non-fixed width integers
operator""_seconds(unsigned long long int value) noexcept;

/// Create a millisecond quantity from an integer literal.
[[nodiscard]] constexpr au::QuantityI64<au::Milliseconds>
// NOLINTNEXTLINE(google-runtime-int) - UDL definitions require non-fixed width integers
operator""_milliseconds(unsigned long long int value) noexcept;

/// Create a nanosecond quantity from an integer literal.
[[nodiscard]] constexpr au::QuantityI64<au::Nanoseconds>
// NOLINTNEXTLINE(google-runtime-int) - UDL definitions require non-fixed width integers
operator""_nanoseconds(unsigned long long int value) noexcept;
} // namespace au

#include "jewels/units/seconds.inl"
