// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/meta/call.hh"

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

namespace jewels::meta
{

struct TestAddConst
{
  template <class T>
  using Type = const T;
};

struct VariadicFn
{
  template <class... T>
  using Type = std::common_type_t<T...>;
};

TEST_CASE("Test call")
{
  static_assert(std::is_same_v<Call<TestAddConst, int>, const int>);
  static_assert(std::is_same_v<Call<VariadicFn, int, const int, int&, const int&>, int>);
}

} // namespace jewels::meta
