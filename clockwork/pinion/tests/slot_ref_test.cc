// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/buffer_index.hh"
#include "clockwork/pinion/buffer_layout.hh"
#include "clockwork/pinion/publisher_slot_ref.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/slot_ref.hh"
#include "clockwork/pinion/tests/support/mock_buffer.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/std/span.hh"

#include <boost/iterator/iterator_facade.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <iterator>
#include <memory_resource>
#include <ranges>
#include <span>
#include <type_traits>

namespace clockwork::pinion
{
namespace
{
TEST_CASE("buffer backed message handle")
{
  constexpr BufferLayout layout{
    .num_slots = 2UL,
    .message_size = 1234UL,
    .is_published_once = false,
  };

  ::jewels::memory::MemoryResource mem_res{std::pmr::get_default_resource()};
  support::BufferStorage<layout> buffer_storage{};
  const auto storage_span = as_writable_bytes(jewels::as_single_item_span(buffer_storage))
                              .first(sizeof(buffer_storage) - support::storage_trail_padding<layout>);
  auto maybe_buffer = Buffer::try_make(storage_span, layout);
  REQUIRE(maybe_buffer);
  auto buffer = *maybe_buffer;
  auto buffer_ptr = jewels::memory::make_non_null_from_ref(buffer);

  REQUIRE(buffer.begin() == buffer.end());

  const auto buffer_iterator1 = buffer.end();
  REQUIRE(buffer.increment_head(BufferIndex{0U}, 1UL));
  buffer_iterator1.dereference().header()->sequence_number = 0;
  const auto message_handle1 = SlotRef(buffer_ptr, buffer_iterator1);
  REQUIRE(message_handle1.is_valid());
  REQUIRE(message_handle1.slot().header()->sequence_number == 0);

  const auto buffer_iterator2 = buffer.end();
  REQUIRE(buffer.increment_head(BufferIndex{1U}, 1UL));
  buffer_iterator2.dereference().header()->sequence_number = 1;
  const auto message_handle2 = SlotRef(buffer_ptr, buffer_iterator2);
  REQUIRE(message_handle1.is_valid());
  REQUIRE(message_handle2.is_valid());
  REQUIRE(message_handle2.slot().header()->sequence_number == 1);

  REQUIRE(buffer.increment_tail(BufferIndex{0U}, 1UL));
  const auto buffer_iterator3 = buffer.end();
  REQUIRE(buffer.increment_head(BufferIndex{2U}, 1UL));
  const auto message_handle3 = SlotRef(buffer_ptr, buffer_iterator3);
  REQUIRE_FALSE(message_handle1.is_valid());
  REQUIRE(message_handle2.is_valid());
  REQUIRE(message_handle3.is_valid());

  REQUIRE(buffer.increment_tail(BufferIndex{1U}, 1UL));
  const auto buffer_iterator4 = buffer.end();
  REQUIRE(buffer.increment_head(BufferIndex{3U}, 1UL));
  const auto message_handle4 = SlotRef(buffer_ptr, buffer_iterator4);
  REQUIRE_FALSE(message_handle1.is_valid());
  REQUIRE_FALSE(message_handle2.is_valid());
  REQUIRE(message_handle3.is_valid());
  REQUIRE(message_handle4.is_valid());

  REQUIRE(buffer.increment_tail(BufferIndex{2U}, 1UL));
  REQUIRE_FALSE(message_handle1.is_valid());
  REQUIRE_FALSE(message_handle2.is_valid());
  REQUIRE_FALSE(message_handle3.is_valid());
  REQUIRE(message_handle4.is_valid());

  REQUIRE(buffer.increment_tail(BufferIndex{3U}, 1UL));
  REQUIRE_FALSE(message_handle1.is_valid());
  REQUIRE_FALSE(message_handle2.is_valid());
  REQUIRE_FALSE(message_handle3.is_valid());
  REQUIRE_FALSE(message_handle4.is_valid());
}

TEST_CASE("SlotRef Iter")
{
  STATIC_REQUIRE(std::random_access_iterator<SlotRef>);
  STATIC_REQUIRE(std::is_same_v<std::iterator_traits<SlotRef>::iterator_category, std::random_access_iterator_tag>);

  ::jewels::memory::MemoryResource mem_res{std::pmr::get_default_resource()};

  constexpr size_t num_slots = 10U;
  constexpr BufferLayout layout{
    .num_slots = num_slots,
    .message_size = 1234UL,
    .is_published_once = false,
  };
  support::BufferStorage<layout> buffer_storage{};
  const auto storage_span = as_writable_bytes(jewels::as_single_item_span(buffer_storage))
                              .first(sizeof(buffer_storage) - support::storage_trail_padding<layout>);
  auto maybe_buffer = Buffer::try_make(storage_span, layout);
  REQUIRE(maybe_buffer);
  auto buffer = *maybe_buffer;
  auto buffer_ptr = jewels::memory::make_non_null_from_ref(buffer);

  {
    auto hide = buffer_ptr->reserve(num_slots);
    REQUIRE(hide);
    PublisherReservation publisher{nullptr, buffer_ptr, *hide, num_slots, true};
    REQUIRE(publisher.slots().size() == num_slots);
    REQUIRE(publisher.commit({}));
  }

  CHECK(std::distance(buffer_ptr->begin(), buffer_ptr->end()) == 10);

  auto start = SlotRef(buffer_ptr, buffer_ptr->begin());
  auto end = SlotRef(buffer_ptr, buffer_ptr->end());
  auto sentinel = SlotRef(buffer_ptr, BufferIterator{});
  auto empty = SlotRef();

  CHECK(start.index() == 0);
  CHECK(std::prev(end).index() == 9);
  CHECK(end - start == num_slots);
  CHECK(std::distance(start, end) == num_slots);
  CHECK(start.is_valid());
  CHECK_FALSE(end.is_valid());
  CHECK_FALSE(sentinel.is_valid());
  CHECK(sentinel.is_sentinel());
  CHECK_FALSE(empty.is_valid());
  CHECK(empty.is_sentinel());

  SECTION("total order")
  {
    CHECK(start == start);
    CHECK_FALSE(start != start);
    CHECK_FALSE(start < start);
    CHECK(start <= start);
    CHECK_FALSE(start > start);
    CHECK(start >= start);

    CHECK_FALSE(start == end);
    CHECK(start != end);
    CHECK(start < end);
    CHECK(start <= end);
    CHECK_FALSE(start > end);
    CHECK_FALSE(start >= end);

    CHECK(sentinel == empty);
    CHECK_FALSE(sentinel != empty);
    CHECK_FALSE(sentinel < empty);
    CHECK(sentinel <= empty);
    CHECK_FALSE(sentinel > empty);
    CHECK(sentinel >= empty);
  }

  SECTION("post-ops")
  {
    CHECK((start--).is_valid());
    CHECK_FALSE((start++).is_valid());
    CHECK_FALSE((end--).is_valid());
    CHECK((end++).is_valid());
  }

  SECTION("pre-ops")
  {
    CHECK_FALSE((--start).is_valid());
    CHECK((++start).is_valid());
    CHECK((--end).is_valid());
    CHECK_FALSE((++end).is_valid());
  }

  SECTION("traverse")
  {
    CHECK(std::distance(start, end) == layout.num_slots);

    auto iter = start;
    for (size_t i = 0; i < layout.num_slots; i++, ++iter)
    {
      CHECK(iter.is_valid());
      CHECK(iter != end);
      CHECK(iter - start == static_cast<std::ptrdiff_t>(i));
    }
    CHECK(iter == end);
    iter -= layout.num_slots;
    CHECK(iter == start);
    iter = layout.num_slots + iter;
    CHECK(iter == end);
  }
}

} // namespace
} // namespace clockwork::pinion
