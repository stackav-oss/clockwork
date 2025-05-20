// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/aligned_pointer.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/buffer_index.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/tests/support/mock_buffer.hh"
#include "clockwork/pinion/tests/support/mock_slot.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/std/span.hh"

#include <__stddef_offsetof.h>
#include <boost/iterator/iterator_facade.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <limits>
#include <ranges>
#include <span>

namespace clockwork::pinion
{

TEST_CASE("Check concepts")
{
  STATIC_REQUIRE(std::random_access_iterator<BufferIterator>);
}

TEST_CASE("Check constants")
{
  REQUIRE(BufferLayout::control_block_size == sizeof(support::ControlBlock));
  SECTION("Size < alignment")
  {
    constexpr BufferLayout layout{
      .num_slots = 2UL,
      .message_size = 8UL,
    };
    REQUIRE(slot_stride(layout) == 2U);
    REQUIRE(buffer_size(layout) == sizeof(support::BufferStorage<layout>) - support::storage_trail_padding<layout>);
    REQUIRE(head_offset(layout) == offsetof(support::BufferStorage<layout>, control_block.head));
    REQUIRE(tail_offset(layout) == offsetof(support::BufferStorage<layout>, control_block.tail));
  }
  SECTION("Size > alignment")
  {
    constexpr BufferLayout layout{
      .num_slots = 2UL,
      .message_size = 1234UL,
    };
    REQUIRE(slot_stride(layout) == 21U);
    REQUIRE(buffer_size(layout) == sizeof(support::BufferStorage<layout>) - support::storage_trail_padding<layout>);
    REQUIRE(head_offset(layout) == offsetof(support::BufferStorage<layout>, control_block.head));
    REQUIRE(tail_offset(layout) == offsetof(support::BufferStorage<layout>, control_block.tail));
  }
}

TEST_CASE("Buffer")
{
  constexpr BufferLayout layout{
    .num_slots = 2UL,
    .message_size = 1234UL,
  };

  SECTION("to_position")
  {
    REQUIRE(detail::to_position(BufferIndex{0UL}, layout) == 0UL);
    REQUIRE(detail::to_position(BufferIndex{1UL}, layout) == 1UL);
    REQUIRE(detail::to_position(BufferIndex{2UL}, layout) == 0UL);
    REQUIRE(detail::to_position(BufferIndex{3UL}, layout) == 1UL);
  }

  SECTION("Construction")
  {
    alignas(Slot::slot_alignment) std::array<std::byte, buffer_size(layout) + 1UL> storage{};
    const auto storage_span = as_writable_bytes(std::span{storage});
    REQUIRE(Buffer::try_make(storage_span, layout) == jewels::unexpected{InitError::invalid_size});
    REQUIRE(Buffer::try_make(storage_span.subspan(2UL), layout) == jewels::unexpected{InitError::invalid_size});
    REQUIRE(Buffer::try_make(storage_span.subspan(1UL), layout) == jewels::unexpected{InitError::invalid_alignment});
    REQUIRE(Buffer::try_make(storage_span.first(buffer_size(layout)), layout));
  }

  support::BufferStorage<layout> buffer_storage{};
  const auto storage_span = as_writable_bytes(jewels::as_single_item_span(buffer_storage))
                              .first(sizeof(buffer_storage) - support::storage_trail_padding<layout>);
  auto maybe_buffer = Buffer::try_make(storage_span, layout);
  REQUIRE(maybe_buffer);

  auto buffer = *maybe_buffer;
  REQUIRE(std::ranges::equal(buffer.bytes(), storage_span));
  REQUIRE(static_cast<const void*>(buffer.get().get()) == static_cast<const void*>(&buffer_storage));

  SECTION("Access / updates")
  {
    REQUIRE(buffer.head() == BufferIndex{0UL});
    REQUIRE(buffer.tail() == BufferIndex{0UL});
    REQUIRE(buffer.increment_head(BufferIndex{0UL}, 1UL) == BufferIndex{1UL});
    REQUIRE(buffer.increment_tail(BufferIndex{0UL}, 1UL) == BufferIndex{1UL});
    REQUIRE(buffer.increment_head(BufferIndex{0UL}, 1UL) == jewels::unexpected{BufferIndex{1UL}});
    REQUIRE(buffer.increment_head(BufferIndex{0UL}, 1UL) == jewels::unexpected{BufferIndex{1UL}});
    REQUIRE(buffer.increment_tail(BufferIndex{2UL}, 1UL) == jewels::unexpected{BufferIndex{1UL}});
    REQUIRE(buffer.increment_tail(BufferIndex{2UL}, 1UL) == jewels::unexpected{BufferIndex{1UL}});
    REQUIRE(buffer.head() == BufferIndex{1UL});
    REQUIRE(buffer.tail() == BufferIndex{1UL});
    buffer_storage.control_block.head = BufferIndex{1UL};
    buffer_storage.control_block.tail = BufferIndex{3UL};
    REQUIRE(buffer.head() == BufferIndex{1UL});
    REQUIRE(buffer.tail() == BufferIndex{3UL});

    constexpr auto max_index{std::numeric_limits<BufferIndex>::max()};
    buffer_storage.control_block.head = max_index;
    REQUIRE(buffer.increment_head(max_index, 1UL) == jewels::unexpected{max_index});
    buffer_storage.control_block.tail = max_index;
    REQUIRE(buffer.increment_tail(max_index, 1UL) == jewels::unexpected{max_index});
  }

  auto index_range = std::ranges::views::iota(0UL, buffer_storage.slots.size());
  for (auto index : index_range)
  {
    buffer_storage.slots.at(index).header.sequence_number = index;
  }

  SECTION("Iteration")
  {
    // Empty
    REQUIRE(std::ranges::empty(buffer));
    REQUIRE(std::end(buffer) - std::begin(buffer) == 0);
    REQUIRE(std::begin(buffer) - std::end(buffer) == 0);

    // One element
    REQUIRE(buffer.increment_head(BufferIndex{0UL}, 1UL));
    REQUIRE_FALSE(std::ranges::empty(buffer));
    REQUIRE(std::ranges::size(buffer) == 1U);
    REQUIRE(buffer.begin()->header()->sequence_number == 0U);
    REQUIRE(std::prev(buffer.end())->header()->sequence_number == 0U);
    REQUIRE(std::next(std::begin(buffer)) == std::end(buffer));
    REQUIRE(std::begin(buffer) == std::prev(std::end(buffer)));
    REQUIRE(std::end(buffer) - std::begin(buffer) == 1);
    REQUIRE(std::begin(buffer) - std::end(buffer) == -1);
    REQUIRE(std::begin(buffer).index() == 0U);
    REQUIRE(std::end(buffer).index() == 1U);

    // Two elements - continguous
    REQUIRE(buffer.increment_head(BufferIndex{1UL}, 1UL));
    REQUIRE(std::ranges::size(buffer) == 2U);
    REQUIRE(buffer.begin()->header()->sequence_number == 0U);
    REQUIRE(std::prev(buffer.end(), 2U)->header()->sequence_number == 0U);
    REQUIRE((-- --buffer.end())->header()->sequence_number == 0U);
    REQUIRE(std::next(buffer.begin())->header()->sequence_number == 1U);
    REQUIRE(std::prev(buffer.end())->header()->sequence_number == 1U);
    REQUIRE((++buffer.begin())->header()->sequence_number == 1U);
    REQUIRE((--buffer.end())->header()->sequence_number == 1U);
    REQUIRE(buffer.begin() == -- --buffer.end());
    REQUIRE(++ ++buffer.begin() == buffer.end());
    REQUIRE(std::end(buffer) - std::begin(buffer) == 2);
    REQUIRE(std::begin(buffer) - std::end(buffer) == -2);

    // One element - at end
    REQUIRE(buffer.increment_tail(BufferIndex{0UL}, 1UL));
    REQUIRE_FALSE(std::ranges::empty(buffer));
    REQUIRE(std::ranges::size(buffer) == 1U);
    REQUIRE(buffer.begin()->header()->sequence_number == 1U);
    REQUIRE(std::prev(buffer.end())->header()->sequence_number == 1U);
    REQUIRE(std::next(std::begin(buffer)) == std::end(buffer));
    REQUIRE(std::begin(buffer) == std::prev(std::end(buffer)));
    REQUIRE(std::end(buffer) - std::begin(buffer) == 1);
    REQUIRE(std::begin(buffer) - std::end(buffer) == -1);

    // Two elements - not contiguous
    REQUIRE(buffer.increment_head(BufferIndex{2UL}, 1UL));
    REQUIRE(std::ranges::size(buffer) == 2U);
    REQUIRE(buffer.begin()->header()->sequence_number == 1U);
    REQUIRE(std::prev(buffer.end(), 2U)->header()->sequence_number == 1U);
    REQUIRE((-- --buffer.end())->header()->sequence_number == 1U);
    REQUIRE(std::next(buffer.begin())->header()->sequence_number == 0U);
    REQUIRE(std::prev(buffer.end())->header()->sequence_number == 0U);
    REQUIRE((++buffer.begin())->header()->sequence_number == 0U);
    REQUIRE((--buffer.end())->header()->sequence_number == 0U);
    REQUIRE(buffer.begin() == -- --buffer.end());
    REQUIRE(++ ++buffer.begin() == buffer.end());
    REQUIRE(std::end(buffer) - std::begin(buffer) == 2);
    REQUIRE(std::begin(buffer) - std::end(buffer) == -2);
  }

  SECTION("Random access iterator")
  {
    REQUIRE(std::ranges::empty(buffer));
    REQUIRE(buffer.increment_head(BufferIndex{0UL}, 1UL));
    REQUIRE(buffer.increment_head(BufferIndex{1UL}, 1UL));

    REQUIRE(buffer.begin()[0].header()->sequence_number == 0U);
    REQUIRE(buffer.begin()[1].header()->sequence_number == 1U);

    REQUIRE(buffer.increment_tail(BufferIndex{0UL}, 1UL));
    REQUIRE(buffer.increment_head(BufferIndex{2UL}, 1UL));

    REQUIRE(buffer.begin()[0].header()->sequence_number == 1U);
    REQUIRE(buffer.begin()[1].header()->sequence_number == 0U);
  }

  SECTION("Batch increments")
  {
    REQUIRE(std::ranges::empty(buffer));
    REQUIRE(buffer.increment_head(BufferIndex{0UL}, 2UL) == BufferIndex{2UL});

    REQUIRE(std::ranges::size(buffer) == 2L);
    REQUIRE(buffer.begin()[0].header()->sequence_number == 0U);
    REQUIRE(buffer.begin()[1].header()->sequence_number == 1U);

    REQUIRE(buffer.increment_tail(BufferIndex{0UL}, 2UL) == BufferIndex{2UL});
    REQUIRE(std::ranges::empty(buffer));
  }
}

} // namespace clockwork::pinion
