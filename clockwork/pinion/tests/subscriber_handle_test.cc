// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/buffer_index.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/slot_ref.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "clockwork/pinion/tests/support/mock_buffer.hh"
#include "clockwork/pinion/tests/support/mock_slot.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/std/span.hh"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <iterator>
#include <new>
#include <ranges>
#include <span>

namespace clockwork::pinion
{

TEST_CASE("Subscriber handle")
{
  constexpr BufferLayout layout{
    .num_slots = 2UL,
    .message_size = 1234UL,
    .is_published_once = false,
  };
  support::BufferStorage<layout> buffer_storage{};
  buffer_storage.slots.at(0U).header.sequence_number = 0UL;
  buffer_storage.slots.at(1U).header.sequence_number = 1UL;
  const auto storage_span = as_writable_bytes(jewels::as_single_item_span(buffer_storage))
                              .first(sizeof(buffer_storage) - support::storage_trail_padding<layout>);
  auto maybe_buffer = Buffer::try_make(storage_span, layout);
  REQUIRE(maybe_buffer);
  auto buffer = *maybe_buffer;

  const SubscriberHandle handle{jewels::memory::make_non_null_from_ref(buffer)};
  REQUIRE(handle.buffer().layout().num_slots == layout.num_slots);
  REQUIRE(handle.buffer().layout().message_size == layout.message_size);

  // Empty
  REQUIRE(std::ranges::empty(handle.available()));

  // Size 1 no wrap
  // Available: 0
  REQUIRE(buffer.increment_head(BufferIndex{0UL}, 1UL) == BufferIndex{1UL});
  REQUIRE_FALSE(std::ranges::empty(handle.available()));
  REQUIRE(std::ranges::size(handle.available()) == 1UL);

  const auto element_zero = std::begin(handle.available());
  REQUIRE(element_zero.is_valid());
  REQUIRE(element_zero->header()->sequence_number == 0UL);

  // Size 2 no wrap
  // Available: 0, 1
  REQUIRE(buffer.increment_head(BufferIndex{1UL}, 1UL) == BufferIndex{2UL});
  REQUIRE_FALSE(std::ranges::empty(handle.available()));
  REQUIRE(std::ranges::size(handle.available()) == 2UL);

  const auto element_one = std::next(std::begin(handle.available()));
  REQUIRE(std::next(element_zero) == element_one);
  REQUIRE(element_zero.is_valid());
  REQUIRE(element_one.is_valid());
  REQUIRE(element_one->header()->sequence_number == 1UL);

  // Size 1 no wrap
  // Available: 1
  REQUIRE(buffer.increment_tail(BufferIndex{0UL}, 1UL) == BufferIndex{1UL});
  REQUIRE_FALSE(std::ranges::empty(handle.available()));
  REQUIRE(std::ranges::size(handle.available()) == 1UL);

  REQUIRE_FALSE(element_zero.is_valid());
  REQUIRE(element_one.is_valid());

  // Size 2 with wrap
  // Available: 1, 2
  REQUIRE(buffer.increment_head(BufferIndex{2UL}, 1UL) == BufferIndex{3UL});
  REQUIRE_FALSE(std::ranges::empty(handle.available()));
  REQUIRE(std::ranges::size(handle.available()) == 2UL);

  const auto element_two = std::next(std::begin(handle.available()));
  REQUIRE(std::next(element_one) == element_two);
  REQUIRE_FALSE(element_zero.is_valid());
  REQUIRE(element_one.is_valid());
  REQUIRE(element_two.is_valid());
  REQUIRE(element_two->header()->sequence_number == 0UL);

  // Size 1 no wrap
  // Available: 2
  REQUIRE(buffer.increment_tail(BufferIndex{1UL}, 1UL) == BufferIndex{2UL});
  REQUIRE_FALSE(std::ranges::empty(handle.available()));
  REQUIRE(std::ranges::size(handle.available()) == 1UL);

  REQUIRE_FALSE(element_zero.is_valid());
  REQUIRE_FALSE(element_one.is_valid());
  REQUIRE(element_two.is_valid());

  // Size 2 no wrap
  // Available: 2, 3
  REQUIRE(buffer.increment_head(BufferIndex{3UL}, 1UL) == BufferIndex{4UL});
  REQUIRE_FALSE(std::ranges::empty(handle.available()));
  REQUIRE(std::ranges::size(handle.available()) == 2UL);

  const auto element_three = std::next(std::begin(handle.available()));
  REQUIRE(std::next(element_two) == element_three);
  REQUIRE_FALSE(element_zero.is_valid());
  REQUIRE_FALSE(element_one.is_valid());
  REQUIRE(element_two.is_valid());
  REQUIRE(element_three.is_valid());
  REQUIRE(element_three->header()->sequence_number == 1UL);

  // Size 1 no wrap
  // Available: 3
  REQUIRE(buffer.increment_tail(BufferIndex{2UL}, 1UL) == BufferIndex{3UL});
  REQUIRE_FALSE(std::ranges::empty(handle.available()));
  REQUIRE(std::ranges::size(handle.available()) == 1UL);

  REQUIRE_FALSE(element_zero.is_valid());
  REQUIRE_FALSE(element_one.is_valid());
  REQUIRE_FALSE(element_two.is_valid());
  REQUIRE(element_three.is_valid());

  // Empty
  REQUIRE(buffer.increment_tail(BufferIndex{3UL}, 1UL) == BufferIndex{4UL});
  REQUIRE(std::ranges::empty(handle.available()));

  REQUIRE_FALSE(element_zero.is_valid());
  REQUIRE_FALSE(element_one.is_valid());
  REQUIRE_FALSE(element_two.is_valid());
  REQUIRE_FALSE(element_three.is_valid());
}

TEST_CASE("Available after")
{
  constexpr BufferLayout layout{
    .num_slots = 4UL,
    .message_size = 1234UL,
    .is_published_once = false,
  };
  support::BufferStorage<layout> buffer_storage{};
  const auto storage_span = as_writable_bytes(jewels::as_single_item_span(buffer_storage))
                              .first(sizeof(buffer_storage) - support::storage_trail_padding<layout>);
  auto maybe_buffer = Buffer::try_make(storage_span, layout);
  REQUIRE(maybe_buffer);
  auto buffer = *maybe_buffer;
  auto mk_slot_ref = [&buffer](BufferIterator iter)
  { return SlotRef(jewels::memory::make_non_null_from_ref(buffer), iter); };

  REQUIRE(buffer.increment_head(BufferIndex{0UL}, 1UL) == BufferIndex{1UL});
  const auto zero_element_iter = mk_slot_ref(std::begin(buffer));
  REQUIRE(buffer.increment_head(BufferIndex{1UL}, 1UL) == BufferIndex{2UL});
  REQUIRE(buffer.increment_head(BufferIndex{2UL}, 1UL) == BufferIndex{3UL});
  REQUIRE(buffer.increment_tail(BufferIndex{0UL}, 1UL) == BufferIndex{1UL});

  auto buffer_span = std::ranges::subrange<SlotRef>{mk_slot_ref(std::begin(buffer)), mk_slot_ref(std::end(buffer))};
  SECTION("Sentinel iterator")
  {
    const auto range = available_starting_from(buffer_span, {});
    REQUIRE(range);
    REQUIRE(std::ranges::size(*range) == 2UL);
    REQUIRE(std::begin(*range) == mk_slot_ref(std::begin(buffer)));
    REQUIRE(std::end(*range) == mk_slot_ref(std::end(buffer)));
  }

  SECTION("Zero - in the past")
  {
    const auto range = available_starting_from(buffer_span, zero_element_iter);
    REQUIRE(range == jewels::unexpected{ProgressError::fell_behind});
  }

  SECTION("First - valid")
  {
    const auto range = available_starting_from(buffer_span, std::next(zero_element_iter));
    REQUIRE(range);
    REQUIRE(std::ranges::size(*range) == 2UL);
    REQUIRE(std::begin(*range) == mk_slot_ref(std::begin(buffer)));
    REQUIRE(std::end(*range) == mk_slot_ref(std::end(buffer)));
  }

  SECTION("Second - valid")
  {
    const auto range = available_starting_from(buffer_span, std::next(zero_element_iter, 2UL));
    REQUIRE(range);
    REQUIRE(std::ranges::size(*range) == 1UL);
    REQUIRE(std::begin(*range) == mk_slot_ref(std::next(std::begin(buffer))));
    REQUIRE(std::end(*range) == mk_slot_ref(std::end(buffer)));
  }

  SECTION("Third - valid")
  {
    const auto range = available_starting_from(buffer_span, std::next(zero_element_iter, 3UL));
    REQUIRE(range);
    REQUIRE(std::ranges::empty(*range));
  }

  SECTION("In the future")
  {
    const auto range = available_starting_from(buffer_span, std::next(zero_element_iter, 4UL));
    REQUIRE(range == jewels::unexpected{ProgressError::in_the_future});
  }
}

TEST_CASE("Message range")
{
  constexpr BufferLayout layout{
    .num_slots = 2UL,
    .message_size = 8UL,
    .is_published_once = false,
  };
  support::BufferStorage<layout> buffer_storage{};
  new (buffer_storage.slots.at(0U).message.data()) uint64_t{0UL};
  new (buffer_storage.slots.at(1U).message.data()) uint64_t{1UL};
  const auto storage_span = as_writable_bytes(jewels::as_single_item_span(buffer_storage))
                              .first(sizeof(buffer_storage) - support::storage_trail_padding<layout>);
  auto maybe_buffer = Buffer::try_make(storage_span, layout);
  REQUIRE(maybe_buffer);
  auto buffer = *maybe_buffer;

  const SubscriberHandle handle{jewels::memory::make_non_null_from_ref(buffer)};
  SECTION("Empty")
  {
    auto empty_range = to_message_range<const uint64_t>(handle.available());
    REQUIRE(empty_range);
    REQUIRE(std::ranges::empty(*empty_range));
  }

  SECTION("Invalid message size")
  {
    REQUIRE(buffer.increment_head(BufferIndex{0UL}, 1UL) == BufferIndex{1UL});
    REQUIRE(to_message_range<const uint32_t>(handle.available()) == jewels::unexpected{jewels::MonoError{}});
  }

  SECTION("Varying sizes")
  {
    REQUIRE(buffer.increment_head(BufferIndex{0UL}, 1UL) == BufferIndex{1UL});
    {
      auto message_range = to_message_range<const uint64_t>(handle.available());
      REQUIRE(message_range);
      REQUIRE(std::ranges::equal(*message_range, std::ranges::views::iota(0UL, 1UL)));
    }

    REQUIRE(buffer.increment_head(BufferIndex{1UL}, 1UL) == BufferIndex{2UL});
    {
      auto message_range = to_message_range<const uint64_t>(handle.available());
      REQUIRE(message_range);
      REQUIRE(std::ranges::equal(*message_range, std::ranges::views::iota(0UL, 2UL)));
    }

    REQUIRE(buffer.increment_tail(BufferIndex{0UL}, 1UL) == BufferIndex{1UL});
    {
      auto message_range = to_message_range<const uint64_t>(handle.available());
      REQUIRE(message_range);
      REQUIRE(std::ranges::equal(*message_range, std::ranges::views::iota(1UL, 2UL)));
    }

    REQUIRE(buffer.increment_tail(BufferIndex{1UL}, 1UL) == BufferIndex{2UL});
    {
      auto message_range = to_message_range<const uint64_t>(handle.available());
      REQUIRE(message_range);
      REQUIRE(std::ranges::empty(*message_range));
    }
  }
}

} // namespace clockwork::pinion
