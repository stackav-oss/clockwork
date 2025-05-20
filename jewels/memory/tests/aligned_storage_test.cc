// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/memory/aligned_storage.hh"
#include "jewels/memory/bits.hh"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>

namespace jewels::memory::testing
{

struct alignas(128) OverAligned
{
};

TEST_CASE("Test alignment")
{
  STATIC_REQUIRE(alignof(AlignedStorage<uint8_t>) == alignof(uint8_t));
  STATIC_REQUIRE(alignof(AlignedStorage<uint16_t>) == alignof(uint16_t));
  STATIC_REQUIRE(alignof(AlignedStorage<uint32_t>) == alignof(uint32_t));
  STATIC_REQUIRE(alignof(AlignedStorage<uint64_t>) == alignof(uint64_t));
  STATIC_REQUIRE(alignof(uint64_t) < alignof(OverAligned));
  STATIC_REQUIRE(alignof(AlignedStorage<OverAligned>) == alignof(OverAligned));
}

TEST_CASE("Test policy construction")
{
  AlignedStorage<uint64_t> storage{};
  ObjectPolicy<uint64_t>::construct(storage, 1234567890U);
  REQUIRE(bit_cast_to<uint64_t>(std::span{storage.bytes}) == 1234567890U);
}

TEST_CASE("Test policy destruction")
{
  static bool destructed{false};
  struct Observable // NOLINT(cppcoreguidelines-special-member-functions)
  {
    ~Observable()
    {
      destructed = true;
    }
  };
  AlignedStorage<Observable> storage{};
  REQUIRE(!destructed);
  ObjectPolicy<Observable>::construct(storage);
  REQUIRE(!destructed);
  ObjectPolicy<Observable>::destruct(storage);
  REQUIRE(destructed);
}

TEST_CASE("Test ptr/get")
{
  STATIC_REQUIRE(
    std::is_same_v<decltype(ObjectPolicy<uint64_t>::ptr(std::declval<AlignedStorage<uint64_t>&>())), uint64_t*>);
  STATIC_REQUIRE(std::is_same_v<
                 decltype(ObjectPolicy<uint64_t>::ptr(std::declval<const AlignedStorage<uint64_t>&>())),
                 const uint64_t*>);
  STATIC_REQUIRE(
    std::is_same_v<decltype(ObjectPolicy<uint64_t>::get(std::declval<AlignedStorage<uint64_t>&>())), uint64_t&>);
  STATIC_REQUIRE(std::is_same_v<
                 decltype(ObjectPolicy<uint64_t>::get(std::declval<const AlignedStorage<uint64_t>&>())),
                 const uint64_t&>);

  AlignedStorage<uint64_t> storage{};
  ObjectPolicy<uint64_t>::construct(storage, 1234567890U);
  REQUIRE(static_cast<void*>(ObjectPolicy<uint64_t>::ptr(storage)) == static_cast<void*>(storage.bytes));
  REQUIRE(
    static_cast<const void*>(ObjectPolicy<uint64_t>::ptr(std::as_const(storage))) ==
    static_cast<const void*>(storage.bytes));
  REQUIRE(ObjectPolicy<uint64_t>::get(std::as_const(storage)) == 1234567890U);
  REQUIRE(ObjectPolicy<uint64_t>::get(storage) == 1234567890U);
  REQUIRE(ObjectPolicy<uint64_t>::get(std::as_const(storage)) == 1234567890U);
}

} // namespace jewels::memory::testing
