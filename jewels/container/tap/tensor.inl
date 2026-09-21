// IWYU pragma: private, include "jewels/container/tap/tensor.hh"
#pragma once

#include "jewels/container/tap/tensor.hh"

#include <algorithm>
#include <span>
#include <utility>

namespace jewels::tap
{

template <typename T, typename Dimensions, typename Strides>
constexpr auto Tensor<T, Dimensions, Strides>::num_dimensions()
{
  return dimensions.size();
}

template <typename T, typename Dimensions, typename Strides>
constexpr auto Tensor<T, Dimensions, Strides>::shape()
{
  return dimensions;
}

template <typename T, typename Dimensions, typename Strides>
constexpr auto Tensor<T, Dimensions, Strides>::layout()
{
  return strides;
}

template <typename T, typename Dimensions, typename Strides>
constexpr std::span<T, Tensor<T, Dimensions, Strides>::num_elements> Tensor<T, Dimensions, Strides>::storage() noexcept
{
  return storage_;
}

template <typename T, typename Dimensions, typename Strides>
constexpr std::span<const T, Tensor<T, Dimensions, Strides>::num_elements>
Tensor<T, Dimensions, Strides>::storage() const noexcept
{
  return storage_;
}

template <typename T, typename Dimensions, typename Strides>
constexpr bool
operator==(const Tensor<T, Dimensions, Strides>& tensor_a, const Tensor<T, Dimensions, Strides>& tensor_b) noexcept(
  noexcept(std::declval<const T&>() == std::declval<const T&>()))
{
  return std::ranges::equal(tensor_a.storage(), tensor_b.storage());
}

} // namespace jewels::tap
