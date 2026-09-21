// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/container/tap/tensor.hh"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace jewels
{

struct ThrowingDestructor
{
  ThrowingDestructor() = default;
  ThrowingDestructor(const ThrowingDestructor&) = default;
  ThrowingDestructor(ThrowingDestructor&&) = default;
  ThrowingDestructor& operator=(const ThrowingDestructor&) = default;
  ThrowingDestructor& operator=(ThrowingDestructor&&) = default;
  ~ThrowingDestructor() noexcept(false) = default;
};

TEST_CASE("Member Types")
{
  tap::Tensor<int16_t, tap::Sizes<2, 3, 4>, tap::Sizes<1, 2, 6>> tensor{};
  STATIC_REQUIRE(tensor.shape() == std::array<size_t, 3>{2, 3, 4});
  STATIC_REQUIRE(tensor.layout() == std::array<size_t, 3>{1, 2, 6});
  STATIC_REQUIRE(tensor.num_dimensions() == 3ULL);
  STATIC_REQUIRE(decltype(tensor)::num_elements == 24ULL);
  REQUIRE(decltype(tensor)::num_elements == tensor.storage().size());
  STATIC_REQUIRE(noexcept(tensor.storage()));
  STATIC_REQUIRE(noexcept(std::as_const(tensor).storage()));
  using ThrowingTensor = tap::Tensor<ThrowingDestructor, tap::Sizes<1>, tap::Sizes<1>>;
  STATIC_REQUIRE(!noexcept(std::declval<ThrowingTensor&>().~ThrowingTensor()));
}

TEST_CASE("Equality")
{
  tap::Tensor<int16_t, tap::Sizes<2, 3, 4>, tap::Sizes<1, 2, 6>> tensor_a{};
  tap::Tensor<int16_t, tap::Sizes<2, 3, 4>, tap::Sizes<1, 2, 6>> tensor_b{};
  REQUIRE(tensor_a == tensor_b);
  REQUIRE_FALSE(tensor_a != tensor_b);
  tensor_a.storage()[0] = -123;
  REQUIRE(tensor_a != tensor_b);
  REQUIRE_FALSE(tensor_a == tensor_b);
}

} // namespace jewels
