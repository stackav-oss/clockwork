// IWYU pragma: private, include "jewels/container/compare.hh"
#pragma once

#include "jewels/container/compare.hh"

#include <array>
#include <cstddef>

namespace jewels
{
namespace detail
{
template <typename T, size_t n>
constexpr int array_cmp(const std::array<T, n>& lhs, const std::array<T, n>& rhs)
{
  for (size_t idx = 0; idx < n; idx++)
  {
    if (lhs.at(idx) != rhs.at(idx))
    {
      return (lhs.at(idx) <= rhs.at(idx) ? -1 : 1);
    }
  }
  return 0;
}
} // namespace detail

template <typename T, size_t n>
constexpr bool array_eq(const std::array<T, n>& lhs, const std::array<T, n>& rhs)
{
  return detail::array_cmp(lhs, rhs) == 0;
}

template <typename T, size_t n>
constexpr bool array_lt(const std::array<T, n>& lhs, const std::array<T, n>& rhs)
{
  return detail::array_cmp(lhs, rhs) < 0;
}

} // namespace jewels
