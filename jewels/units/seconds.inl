// IWYU pragma: private, include "jewels/units/seconds.hh"
#pragma once

#include "jewels/units/seconds.hh"

#include "jewels/units/quantity.hh"

#include <au/magnitude.hh>

namespace au
{
/// Create a second quantity from an integer literal.
[[nodiscard]] constexpr au::QuantityI64<au::Seconds>
// NOLINTNEXTLINE(google-runtime-int) - UDL definitions require non-fixed width integers
operator""_seconds(unsigned long long int value) noexcept
{
  return au::seconds(value);
}

/// Create a millisecond quantity from an integer literal.
[[nodiscard]] constexpr au::QuantityI64<au::Milliseconds>
// NOLINTNEXTLINE(google-runtime-int) - UDL definitions require non-fixed width integers
operator""_milliseconds(unsigned long long int value) noexcept
{
  return au::milliseconds(value);
}

/// Create a nanosecond quantity from an integer literal.
[[nodiscard]] constexpr au::QuantityI64<au::Nanoseconds>
// NOLINTNEXTLINE(google-runtime-int) - UDL definitions require non-fixed width integers
operator""_nanoseconds(unsigned long long int value) noexcept
{
  return au::nanoseconds(value);
}
} // namespace au
