// IWYU pragma: private, include "jewels/units/minutes.hh"
#pragma once

#include "jewels/units/minutes.hh"

#include "jewels/units/quantity.hh"

namespace au
{
/// Create a minute quantity from an integer literal.
[[nodiscard]] constexpr au::QuantityI64<au::Minutes>
// NOLINTNEXTLINE(google-runtime-int) - UDL definitions require non-fixed width integers
operator""_minutes(unsigned long long int value) noexcept
{
  return au::minutes(value);
}
} // namespace au
