// IWYU pragma: private, include "jewels/units/miles.hh"
#pragma once

#include "jewels/units/miles.hh"

#include "jewels/units/quantity.hh"

namespace au
{
/// Create a mile quantity from an integer literal.
[[nodiscard]] constexpr au::QuantityI64<au::Miles>
// NOLINTNEXTLINE(google-runtime-int) - UDL definitions require non-fixed width integers
operator""_miles(unsigned long long int value) noexcept
{
  return au::miles(value);
}
} // namespace au
