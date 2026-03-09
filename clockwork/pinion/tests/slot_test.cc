// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/aligned_pointer.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/tests/support/mock_slot.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/std/span.hh"

#include <__stddef_offsetof.h>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>

namespace clockwork::pinion
{

TEST_CASE("Check constants")
{
  SECTION("Static")
  {
    STATIC_REQUIRE(Slot::slot_alignment == Slot::header_alignment);
    STATIC_REQUIRE(Slot::header_alignment >= alignof(Header));
    STATIC_REQUIRE(Slot::message_alignment == 64UL);

    STATIC_REQUIRE(Slot::header_offset == offsetof(support::SlotStorage<8U>, header));
    STATIC_REQUIRE(Slot::header_offset == offsetof(support::SlotStorage<12U>, header));
    STATIC_REQUIRE(Slot::message_offset == offsetof(support::SlotStorage<8U>, message));
    STATIC_REQUIRE(Slot::message_offset == offsetof(support::SlotStorage<12U>, message));

    STATIC_REQUIRE(Slot::header_to_message_padding == 32UL);
  }

  SECTION("Message dependent")
  {
    STATIC_REQUIRE(trail_padding_offset(8UL) == Slot::message_offset + 8UL);
    STATIC_REQUIRE(trail_padding_offset(8UL) + trail_padding_size(8UL) == slot_size(8UL));
    STATIC_REQUIRE(trail_padding_offset(12UL) == Slot::message_offset + 12UL);
    STATIC_REQUIRE(trail_padding_offset(12UL) + trail_padding_size(12UL) == slot_size(12UL));

    STATIC_REQUIRE(slot_size(8UL) == sizeof(support::SlotStorage<8UL>));
    STATIC_REQUIRE(slot_size(12UL) == sizeof(support::SlotStorage<12UL>));
  }
}

TEST_CASE("Matching constness byte")
{
  STATIC_REQUIRE(std::is_same_v<ByteMatchingConstnessOf<int64_t>, std::byte>);
  STATIC_REQUIRE(std::is_same_v<ByteMatchingConstnessOf<const int64_t>, const std::byte>);
}

TEST_CASE("Marshal")
{
  uint32_t value{};
  SECTION("const")
  {
    auto maybe_ptr = marshal_as<const uint32_t>(as_bytes(jewels::as_single_item_span(value)));
    STATIC_REQUIRE(std::is_same_v<decltype(maybe_ptr->get()), const uint32_t*>);
    REQUIRE(maybe_ptr);
    REQUIRE(maybe_ptr->get() == &value);
    REQUIRE(&*unsafe_marshal_as<const uint32_t>(as_bytes(jewels::as_single_item_span(value))) == &value);
  }
  SECTION("mutable")
  {
    auto maybe_ptr = marshal_as<uint32_t>(as_writable_bytes(jewels::as_single_item_span(value)));
    STATIC_REQUIRE(std::is_same_v<decltype(maybe_ptr->get()), uint32_t*>);
    REQUIRE(maybe_ptr);
    REQUIRE(maybe_ptr->get() == &value);
    REQUIRE(&*unsafe_marshal_as<uint32_t>(as_writable_bytes(jewels::as_single_item_span(value))) == &value);
  }
  SECTION("Too small")
  {
    auto maybe_ptr = marshal_as<uint64_t>(as_writable_bytes(jewels::as_single_item_span(value)));
    STATIC_REQUIRE(std::is_same_v<decltype(maybe_ptr->get()), uint64_t*>);
    REQUIRE_FALSE(maybe_ptr);
  }
}

TEST_CASE("Slot test")
{
  constexpr auto message_size{12U};
  support::SlotStorage<message_size> storage{};
  auto maybe_aligned_pointer = AlignedPtr<std::byte, Slot::slot_alignment>::try_make(
    jewels::memory::ObjectPtr<std::byte>{as_writable_bytes(jewels::as_single_item_span(storage)).data()});
  REQUIRE(maybe_aligned_pointer);
  Slot slot{*maybe_aligned_pointer, message_size};

  REQUIRE(slot.header().get() == &storage.header);
  REQUIRE(std::as_const(slot).header().get() == &storage.header);

  REQUIRE(slot.message().data() == storage.message.data());
  REQUIRE(std::as_const(slot).message().data() == storage.message.data());

  REQUIRE(slot.message().size() == storage.message.size());
  REQUIRE(std::as_const(slot).message().size() == storage.message.size());

  REQUIRE(static_cast<const void*>(slot.bytes().data()) == static_cast<const void*>(&storage));
  REQUIRE(slot.bytes().size() == sizeof(storage));

  const auto [header_bytes, footer_bytes] = slot.headers_footers();
  REQUIRE(static_cast<const void*>(header_bytes.data()) == static_cast<const void*>(&storage));
  REQUIRE(header_bytes.size() == Slot::message_offset);
  REQUIRE(header_bytes.size() + footer_bytes.size() == sizeof(storage) - sizeof(storage.message));

  STATIC_CHECK(std::is_constructible_v<Slot, AlignedBytePtr<Slot::slot_alignment>, size_t>);
  STATIC_CHECK(!std::is_constructible_v<Slot, ConstAlignedBytePtr<Slot::slot_alignment>, size_t>);
  STATIC_CHECK(std::is_constructible_v<ConstSlot, AlignedBytePtr<Slot::slot_alignment>, size_t>);
  STATIC_CHECK(std::is_constructible_v<ConstSlot, ConstAlignedBytePtr<Slot::slot_alignment>, size_t>);
  STATIC_CHECK(std::is_constructible_v<Slot, Slot>);
  STATIC_CHECK(!std::is_constructible_v<Slot, ConstSlot>);
  STATIC_CHECK(std::is_constructible_v<ConstSlot, Slot>);
  STATIC_CHECK(std::is_constructible_v<ConstSlot, ConstSlot>);
  ConstSlot cslot = slot;
  CHECK(cslot.bytes().data() == slot.bytes().data());
}

} // namespace clockwork::pinion
