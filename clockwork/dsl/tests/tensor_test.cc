// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dsl/tests/support/tensor_msg_clk_cc.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/container/tap/tensor.hh"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace clockwork
{

TEST_CASE("Tensor Message")
{
  Tappy<testing::TensorMessage> msg;

  auto& tensor0 = msg.get_mutable_tensor0();
  STATIC_REQUIRE(std::is_same_v<std::remove_cvref_t<decltype(tensor0)>::ElementType, float>);
  STATIC_REQUIRE(std::remove_cvref_t<decltype(tensor0)>::shape() == std::array<size_t, 3>{5, 6, 7});
  STATIC_REQUIRE(std::remove_cvref_t<decltype(tensor0)>::layout() == std::array<size_t, 3>{1, 5, 30});

  auto& tensor1 = msg.get_mutable_tensor1();
  STATIC_REQUIRE(std::is_same_v<std::remove_cvref_t<decltype(tensor1)>::ElementType, int16_t>);
  STATIC_REQUIRE(std::remove_cvref_t<decltype(tensor1)>::shape() == std::array<size_t, 3>{5, 6, 7});
  STATIC_REQUIRE(std::remove_cvref_t<decltype(tensor1)>::layout() == std::array<size_t, 3>{42, 7, 1});

  auto& tensor2 = msg.get_mutable_tensor2();
  STATIC_REQUIRE(std::is_same_v<std::remove_cvref_t<decltype(tensor2)>::ElementType, bool>);
  STATIC_REQUIRE(std::remove_cvref_t<decltype(tensor2)>::shape() == std::array<size_t, 3>{5, 6, 7});
  STATIC_REQUIRE(std::remove_cvref_t<decltype(tensor2)>::layout() == std::array<size_t, 3>{42, 7, 1});

  auto& tensor3 = msg.get_mutable_tensor3();
  STATIC_REQUIRE(std::is_same_v<std::remove_cvref_t<decltype(tensor3)>::ElementType, external::ExternalStrongType>);

  // clear() should not affect tensor fields.
  tensor0.storage()[0] = 1.1f;
  tensor1.storage()[0] = -123;
  tensor2.storage()[0] = true;
  msg.clear();
  REQUIRE(tensor0.storage()[0] == 1.1f);
  REQUIRE(tensor1.storage()[0] == -123);
  REQUIRE(tensor2.storage()[0] == true);
}

} // namespace clockwork
