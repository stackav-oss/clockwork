// IWYU pragma: private, include "jewels/units/feet.hh"
#pragma once

#include "jewels/units/feet.hh"

#include "jewels/units/quantity.hh"

namespace au
{
/// Create a feet quantity from an integer literal.
[[nodiscard]] constexpr au::QuantityI64<au::Feet>
// NOLINTNEXTLINE(google-runtime-int) - UDL definitions require non-fixed width integers
operator""_feet(unsigned long long int value) noexcept
{
  return au::feet(value);
}
} // namespace au
