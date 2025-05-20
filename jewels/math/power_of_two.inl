// IWYU pragma: private, include "jewels/math/power_of_two.hh"
#pragma once

#include "jewels/math/power_of_two.hh"

#include <cstddef>
#include <type_traits> // IWYU pragma: keep

namespace jewels::math
{

template <class UnsignedInteger>
constexpr bool is_power_of_two(UnsignedInteger power_of_two)
{
  static_assert(std::is_unsigned_v<UnsignedInteger>);
  constexpr auto zero{static_cast<UnsignedInteger>(0UL)};
  constexpr auto one{static_cast<UnsignedInteger>(1UL)};
  return (power_of_two & static_cast<UnsignedInteger>(power_of_two - one)) == zero && power_of_two > zero;
}

template <size_t power_of_two, class UnsignedInteger>
constexpr UnsignedInteger round_up_to_power_of_two_multiple(UnsignedInteger value_to_round)
{
  static_assert(std::is_unsigned_v<UnsignedInteger>);
  static_assert(is_power_of_two(power_of_two));
  constexpr auto one{static_cast<UnsignedInteger>(1UL)};
  constexpr UnsignedInteger mask{power_of_two - one};
  return static_cast<UnsignedInteger>(value_to_round + mask) & static_cast<UnsignedInteger>(~mask);
}

constexpr size_t round_up_to_power_of_two(size_t value_to_round)
{
  constexpr auto one{1U};
  constexpr auto two{2U};
  constexpr auto four{4U};
  constexpr auto eight{8U};
  constexpr auto sixteen{16U};
  constexpr auto thirtytwo{32U};
  value_to_round--;
  value_to_round |= value_to_round >> one;
  value_to_round |= value_to_round >> two;
  value_to_round |= value_to_round >> four;
  value_to_round |= value_to_round >> eight;
  value_to_round |= value_to_round >> sixteen;
  value_to_round |= value_to_round >> thirtytwo;
  value_to_round++;
  return value_to_round;
}

} // namespace jewels::math
