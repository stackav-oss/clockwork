// IWYU pragma: private, include "jewels/units/hours.hh"
#pragma once

#include "jewels/units/hours.hh"

#include "jewels/units/quantity.hh"

namespace au
{
/// Create an hour quantity from an integer literal.
[[nodiscard]] constexpr au::QuantityI64<au::Hours>
// NOLINTNEXTLINE(google-runtime-int) - UDL definitions require non-fixed width integers
operator""_hours(unsigned long long int value) noexcept
{
  return au::hours(value);
}
} // namespace au
