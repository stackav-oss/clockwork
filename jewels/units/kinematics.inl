// IWYU pragma: private, include "jewels/units/kinematics.hh"
#pragma once

#include "jewels/units/kinematics.hh"

#include "jewels/units/quantity.hh"

#include <au/unit_of_measure.hh>

namespace au
{
[[nodiscard]] constexpr au::QuantityI64<au::MetersPerSecond>
// NOLINTNEXTLINE(google-runtime-int) - UDL definitions require non-fixed width integers
operator""_mps(unsigned long long int value) noexcept
{
  return au::mps(value);
}

[[nodiscard]] constexpr au::QuantityI64<au::MetersPerSecondSquared>
// NOLINTNEXTLINE(google-runtime-int) - UDL definitions require non-fixed width integers
operator""_mpss(unsigned long long value) noexcept
{
  return au::mpss(value);
}

} // namespace au
