// IWYU pragma: private, include "jewels/units/pounds_mass.hh"
#pragma once

#include "jewels/units/pounds_mass.hh"

#include "jewels/units/quantity.hh"

namespace au
{
/// Create a pounds mass quantity from an integer literal.
[[nodiscard]] constexpr au::QuantityI64<au::PoundsMass>
// NOLINTNEXTLINE(google-runtime-int) - UDL definitions require non-fixed width integers
operator""_pounds_mass(unsigned long long int value) noexcept
{
  return au::pounds_mass(value);
}
} // namespace au
