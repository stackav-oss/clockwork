// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/container/tap/bitset.hh"
#include "jewels/meta/concepts.hh"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace jewels
{
namespace
{

template <size_t size_p>
constexpr void assert_layout()
{
  using BitsetType = tap::Bitset<size_p>;
  static_assert(sizeof(BitsetType) == BitsetType::byte_size);
  static_assert(alignof(BitsetType) == 1U);
  static_assert(meta::ImplicitLifetimeType<BitsetType>);
  static_assert(std::is_trivially_copyable_v<BitsetType>);
}

template <size_t size_p>
constexpr void assert_noexcept_operations()
{
  using BitsetType = tap::Bitset<size_p>;
  static_assert(noexcept(BitsetType{}));
  static_assert(noexcept(std::declval<BitsetType&>().try_set(0U)));
  static_assert(noexcept(std::declval<BitsetType&>().try_set(0U, true)));
  static_assert(noexcept(std::declval<BitsetType&>().try_reset(0U)));
  static_assert(noexcept(std::declval<BitsetType&>().reset()));
  static_assert(noexcept(std::declval<BitsetType&>().try_flip(0U)));
  static_assert(noexcept(std::declval<BitsetType&>().flip()));
  static_assert(noexcept(std::declval<const BitsetType&>().all()));
  static_assert(noexcept(std::declval<const BitsetType&>().any()));
  static_assert(noexcept(std::declval<const BitsetType&>().none()));
  static_assert(noexcept(std::declval<const BitsetType&>().count()));
  static_assert(noexcept(std::declval<const BitsetType&>().bytes()));
}

constexpr bool constexpr_safe_bitset_operations()
{
  tap::Bitset<10U> bitset{};
  if (
    jewels::fails(bitset.try_set(0U)) || jewels::fails(bitset.try_set(7U)) || jewels::fails(bitset.try_set(9U)) ||
    jewels::fails(bitset.try_reset(7U)) || jewels::fails(bitset.try_flip(9U)))
  {
    return false;
  }
  if (bitset.count() != 1U || !bitset.any() || bitset.all() || bitset.none())
  {
    return false;
  }
  return jewels::fails(bitset.try_set(10U)) && bitset.count() == 1U;
}

static_assert((assert_layout<1U>(), true));
static_assert((assert_layout<8U>(), true));
static_assert((assert_layout<9U>(), true));
static_assert((assert_layout<16U>(), true));
static_assert((assert_layout<17U>(), true));
static_assert((assert_layout<32U>(), true));
static_assert((assert_layout<33U>(), true));
static_assert((assert_layout<64U>(), true));
static_assert((assert_layout<65U>(), true));
static_assert((assert_noexcept_operations<10U>(), true));
static_assert(constexpr_safe_bitset_operations());

TEST_CASE("Bitset default construction and mutation")
{
  tap::Bitset<10U> bitset{};
  REQUIRE(bitset.size() == 10U);
  REQUIRE(bitset.bytes().size() == 2U);
  REQUIRE(bitset.none());
  REQUIRE(bitset.count() == 0U);

  bitset.set(0U).set(7U).set(9U);
  REQUIRE(bitset.test(0U));
  REQUIRE(bitset.test(7U));
  REQUIRE(bitset.test(9U));
  REQUIRE(bitset.count() == 3U);
  REQUIRE(bitset.bytes()[0] == std::byte{0x81});
  REQUIRE(bitset.bytes()[1] == std::byte{0x02});

  bitset.reset(7U).flip(9U);
  REQUIRE(bitset.count() == 1U);
  REQUIRE(bitset.test(0U));
  REQUIRE(bitset.none() == false);

  bitset.flip();
  REQUIRE_FALSE(bitset.test(0U));
  REQUIRE(bitset.count() == 9U);
  bitset.set(0U);
  REQUIRE(bitset.all());
  REQUIRE(bitset.count() == 10U);
  REQUIRE(bitset.bytes()[1] == std::byte{0x03});

  bitset.reset();
  REQUIRE(bitset.none());
}

TEST_CASE("Bitset safe access rejects invalid indexes")
{
  tap::Bitset<9U> bitset{};
  bool value{true};
  REQUIRE(jewels::fails(bitset.test(jewels::Out{value}, 9U)));
  REQUIRE(value);
  REQUIRE(jewels::fails(bitset.try_set(9U)));
  REQUIRE(jewels::fails(bitset.try_set(9U, false)));
  REQUIRE(jewels::fails(bitset.try_reset(9U)));
  REQUIRE(jewels::fails(bitset.try_flip(9U)));
  REQUIRE(bitset.none());
  REQUIRE_THROWS_AS(bitset.test(9U), std::out_of_range);
  REQUIRE_THROWS_AS(bitset.set(9U), std::out_of_range);
  REQUIRE_THROWS_AS(bitset.reset(9U), std::out_of_range);
  REQUIRE_THROWS_AS(bitset.flip(9U), std::out_of_range);
}

TEST_CASE("Bitset callsig mutation and query")
{
  tap::Bitset<10U> bitset{};
  REQUIRE(jewels::ok(bitset.try_set(0U)));
  REQUIRE(jewels::ok(bitset.try_set(7U)));
  REQUIRE(jewels::ok(bitset.try_set(9U)));

  bool value{false};
  REQUIRE(jewels::ok(bitset.test(jewels::Out{value}, 9U)));
  REQUIRE(value);
  REQUIRE(jewels::ok(bitset.try_reset(7U)));
  REQUIRE(jewels::ok(bitset.try_flip(9U)));
  REQUIRE(bitset.count() == 1U);
}

TEST_CASE("Bitset ignores unused high bits in logical operations")
{
  tap::Bitset<9U> canonical{};
  canonical.set(0U);

  tap::Bitset<9U> noncanonical{};
  const std::array<std::byte, 2U> bytes{std::byte{0x01}, std::byte{0xFE}};
  std::memcpy(&noncanonical, bytes.data(), bytes.size());

  REQUIRE(noncanonical.count() == 1U);
  REQUIRE(noncanonical.any());
  REQUIRE_FALSE(noncanonical.all());
  REQUIRE(noncanonical == canonical);
}

} // namespace
} // namespace jewels
