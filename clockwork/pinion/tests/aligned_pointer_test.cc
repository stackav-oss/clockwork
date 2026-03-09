// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/aligned_pointer.hh"
#include "jewels/math/power_of_two.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace clockwork::pinion
{

TEST_CASE("Aligned pointer")
{
  auto to_byte_pointer = [](auto* pointer)
  {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) Test code and alignment must be checked.
    return jewels::memory::ObjectPtr<std::byte>{reinterpret_cast<std::byte*>(pointer)};
  };
  SECTION("Construction")
  {
    constexpr auto alignment{8UL};
    alignas(alignment) std::array<std::byte, 2UL> bytes{};
    REQUIRE(ConstAlignedBytePtr<alignment>::try_make(jewels::memory::make_non_null_from_ref(bytes.front())));
    REQUIRE_FALSE(ConstAlignedBytePtr<alignment>::try_make(jewels::memory::make_non_null_from_ref(bytes.back())));
    struct alignas(alignment)
    {
    } some_aligned_value;
    REQUIRE(
      static_cast<const void*>(AlignedBytePtr<alignment>::from_ref(some_aligned_value).get()) ==
      static_cast<const void*>(&some_aligned_value));
  }
  SECTION("const Construction")
  {
    constexpr auto alignment{8UL};
    alignas(alignment) const std::array<std::byte, 2UL> cbytes{};
    REQUIRE(ConstAlignedBytePtr<alignment>::try_make(jewels::memory::make_non_null_from_ref(cbytes.front())));
  }
  SECTION("const Coversion")
  {
    constexpr auto alignment{8UL};
    alignas(alignment) std::array<std::byte, 2UL> cbytes{};
    auto c_ptr = ConstAlignedBytePtr<alignment>::try_make(jewels::memory::make_non_null_from_ref(cbytes.front()));
    auto m_ptr = AlignedBytePtr<alignment>::try_make(jewels::memory::make_non_null_from_ref(cbytes.front()));
    REQUIRE(c_ptr);
    REQUIRE(m_ptr);
    CHECK(c_ptr->get() == m_ptr->get());
    ConstAlignedBytePtr<alignment> conv1 = *c_ptr;
    ConstAlignedBytePtr<alignment> conv2 = *m_ptr;
    CHECK(conv1.get() == c_ptr->get());
    CHECK(conv2.get() == m_ptr->get());
  }
  SECTION("Access")
  {
    uint64_t value{8UL};
    auto maybe_aligned = AlignedBytePtr<alignof(uint64_t)>::try_make(to_byte_pointer(&value));
    REQUIRE(maybe_aligned);
    auto aligned = *maybe_aligned;
    REQUIRE(static_cast<void*>(aligned.operator->()) == static_cast<void*>(&value));
    REQUIRE(static_cast<void*>(aligned.get()) == static_cast<void*>(&value));
  }
  SECTION("Arithmetic")
  {
    std::array<uint64_t, 2UL> values{};
    auto maybe_aligned = AlignedBytePtr<alignof(uint64_t)>::try_make(to_byte_pointer(values.data()));
    REQUIRE(maybe_aligned);
    auto aligned = *maybe_aligned;
    SECTION("Increment / Decrement")
    {
      REQUIRE(static_cast<const void*>(aligned.get()) == static_cast<const void*>(&values.front()));

      REQUIRE(static_cast<const void*>((++aligned).get()) == static_cast<const void*>(&values.back()));
      REQUIRE(static_cast<const void*>(aligned.get()) == static_cast<const void*>(&values.back()));

      REQUIRE(static_cast<const void*>((--aligned).get()) == static_cast<const void*>(&values.front()));
      REQUIRE(static_cast<const void*>(aligned.get()) == static_cast<const void*>(&values.front()));

      REQUIRE(static_cast<const void*>((aligned++).get()) == static_cast<const void*>(&values.front()));
      REQUIRE(static_cast<const void*>(aligned.get()) == static_cast<const void*>(&values.back()));

      REQUIRE(static_cast<const void*>((aligned--).get()) == static_cast<const void*>(&values.back()));
      REQUIRE(static_cast<const void*>(aligned.get()) == static_cast<const void*>(&values.front()));
    }
    SECTION("Addition / Subtraction")
    {
      REQUIRE(static_cast<const void*>(aligned.get()) == static_cast<const void*>(&values.front()));
      REQUIRE(static_cast<const void*>((aligned + 1U).get()) == static_cast<const void*>(&values.back()));
      REQUIRE(static_cast<const void*>((aligned += 1U).get()) == static_cast<const void*>(&values.back()));
      REQUIRE(static_cast<const void*>(aligned.get()) == static_cast<const void*>(&values.back()));

      REQUIRE(static_cast<const void*>((aligned - 1U).get()) == static_cast<const void*>(&values.front()));
      REQUIRE(static_cast<const void*>((aligned -= 1U).get()) == static_cast<const void*>(&values.front()));
      REQUIRE(static_cast<const void*>(aligned.get()) == static_cast<const void*>(&values.front()));
    }
  }
  SECTION("Equality")
  {
    uint64_t value_a{};
    uint64_t value_b{};
    auto maybe_aligned_a = AlignedBytePtr<alignof(uint64_t)>::try_make(to_byte_pointer(&value_a));
    REQUIRE(maybe_aligned_a);
    auto aligned_a = *maybe_aligned_a;
    auto maybe_bligned_b = AlignedBytePtr<alignof(uint64_t)>::try_make(to_byte_pointer(&value_b));
    REQUIRE(maybe_bligned_b);
    auto aligned_b = *maybe_bligned_b;
    REQUIRE(aligned_a != aligned_b);
    aligned_b = aligned_a;
    REQUIRE(aligned_a == aligned_b);
  }
}

} // namespace clockwork::pinion
