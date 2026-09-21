#pragma once
// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/meta/integer_sequence.hh"

#include <array>
#include <cstddef>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>

namespace jewels::tap
{

/// A helper type for holding a sequence of sizes.
template <size_t... sizes_p>
struct Sizes
{
  static constexpr std::array sizes{sizes_p...};
};

/// Tensor represents a fixed-size multi-dimensional array.
/// @tparam T the tensor's element type.
/// @tparam Dimensions an array of size_t that correspond to the sizes of each of the tensor's dimensions.
/// @tparam Strides an array of size_t that corresponds to the tensor's memory layout.
template <typename T, typename Dimensions, typename Strides>
class Tensor
{
public:
  using ElementType = T;

  static constexpr auto dimensions{Dimensions::sizes};
  static_assert(std::is_same_v<typename decltype(dimensions)::value_type, size_t>);
  // While a zero-dimensional tensor is a real thing, supporting that special
  // case would unnecesarily complicate this type. Just use a scalar type
  // instead.
  static_assert(dimensions.size() > 0);

  static constexpr auto strides{Strides::sizes};
  static_assert(std::is_same_v<typename decltype(strides)::value_type, size_t>);
  static_assert(strides.size() == dimensions.size());

  static constexpr size_t num_elements{
    std::apply([](const auto&... args) { return (size_t{1} * ... * args); }, dimensions)};

  // Make sure that the element (dimensions[0] - 1, ..., dimensions[n-1] - 1) is
  // in bounds.
  static_assert(
    std::apply(
      [](const auto... index) { return (size_t{0} + ... + ((dimensions[index] - 1) * strides[index])); },
      meta::to_array(std::make_index_sequence<dimensions.size()>{})) < num_elements);

  constexpr Tensor() noexcept(std::is_nothrow_default_constructible_v<T>) = default;
  constexpr Tensor(const Tensor&) noexcept(std::is_nothrow_copy_constructible_v<T>) = default;
  constexpr Tensor(Tensor&&) noexcept(std::is_nothrow_move_constructible_v<T>) = default;
  ~Tensor() noexcept(std::is_nothrow_destructible_v<T>) = default;

  constexpr Tensor& operator=(const Tensor&) noexcept(std::is_nothrow_copy_assignable_v<T>) = default;
  constexpr Tensor& operator=(Tensor&&) noexcept(std::is_nothrow_move_assignable_v<T>) = default;

  /// @return The number of dimensions spanned by this tensor.
  static constexpr auto num_dimensions();

  /// @return The shape array for this tensor.
  static constexpr auto shape();

  /// @return The stride array for this tensor.
  static constexpr auto layout();

  /// @return A flat mutable span over this tensor's backing storage.
  constexpr std::span<T, num_elements> storage() noexcept;

  /// @return A flat const span over this tensor's backing storage.
  constexpr std::span<const T, num_elements> storage() const noexcept;

private:
  /// The backing buffer.
  std::array<ElementType, num_elements> storage_;
};

/// Equality operator.
/// @return true if the contents of tensor_a and tensor_b are equivalent.
template <typename T, typename Dimensions, typename Strides>
constexpr bool
operator==(const Tensor<T, Dimensions, Strides>& tensor_a, const Tensor<T, Dimensions, Strides>& tensor_b) noexcept(
  noexcept(std::declval<const T&>() == std::declval<const T&>()));

} // namespace jewels::tap

#include "jewels/container/tap/tensor.inl"
