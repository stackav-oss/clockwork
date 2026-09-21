// IWYU pragma: private, include "jewels/utility/string_param.hh"
#pragma once
#include "jewels/utility/string_param.hh"

#include <array>
#include <cstddef>
#include <string_view>

namespace jewels
{

template <std::size_t string_size>
// NOLINTNEXTLINE(modernize-avoid-c-arrays) C-array is required for this to work
constexpr StringParam<string_size>::StringParam(const char (&str)[string_size])
  : data(std::to_array(str))
{
}

template <std::size_t string_size>
constexpr StringParam<string_size>::operator std::string_view() const
{
  return {data.data(), data.size() - 1U};
};

} // namespace jewels
