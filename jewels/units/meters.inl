// IWYU pragma: private, include "jewels/units/meters.hh"
#pragma once

#include "jewels/units/meters.hh"

#include "jewels/units/quantity.hh"

namespace au
{
[[nodiscard]] constexpr au::QuantityI64<au::Meters>
// NOLINTNEXTLINE(google-runtime-int) - UDL definitions require non-fixed width integers
operator""_meters(unsigned long long int value) noexcept
{
  return au::meters(value);
}

[[nodiscard]] constexpr au::QuantityD<au::Meters> operator""_meters(long double value) noexcept
{
  return au::meters(static_cast<double>(value));
}

[[nodiscard]] constexpr au::QuantityI64<au::Meters>
// NOLINTNEXTLINE(google-runtime-int) - UDL definitions require non-fixed width integers
operator""_m(unsigned long long int value) noexcept
{
  return au::meters(value);
}

[[nodiscard]] constexpr au::QuantityD<au::Meters> operator""_m(long double value) noexcept
{
  return au::meters(static_cast<double>(value));
}
} // namespace au
