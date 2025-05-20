// IWYU pragma: private, include "jewels/units/meters.hh"
#pragma once

#include "jewels/units/meters.hh"

#include "jewels/units/quantity.hh"

namespace au
{
/// Create a meter quantity from an integer literal.
[[nodiscard]] constexpr au::QuantityI64<au::Meters>
// NOLINTNEXTLINE(google-runtime-int) - UDL definitions require non-fixed width integers
operator""_meters(unsigned long long int value) noexcept
{
  return au::meters(value);
}
} // namespace au
