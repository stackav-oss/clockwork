// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/memory/bits.hh"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>

namespace jewels::memory::testing
{

TEST_CASE("Test write_as_bytes")
{
  // make sure each byte has some both zero and non-zero bits.
  unsigned int data{0x12345678};

  std::array<std::byte, sizeof(data)> bytes{};
  write_as_bytes(data, std::span{bytes});

  auto expected_bytes = as_bytes(std::span{&data, 1});
  REQUIRE(std::ranges::equal(expected_bytes, bytes));
}

TEST_CASE("Test bit_cast_to")
{
  // make sure each byte has some both zero and non-zero bits.
  unsigned int data{0x12345678};

  auto bytes = as_bytes(std::span<decltype(data), 1>{&data, 1});
  REQUIRE(bit_cast_to<decltype(data)>(bytes) == data);
}

} // namespace jewels::memory::testing
