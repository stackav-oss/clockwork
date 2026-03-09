// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/container/circular_buffer.hh"
#include "jewels/container/circular_buffer_state_clk_cc.hh"
#include "jewels/memory/aligned_storage.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/meta/overloaded.hh"
#include "jewels/std/expected.hh"

#include <boost/iterator/iterator_facade.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/interfaces/catch_interfaces_capture.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory_resource>
#include <numeric>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace jewels::container::testing
{

template <class Container>
struct ClearPolicy
{
  using storage_type = Container;
  using value_type = storage_type;
  using reference = value_type&;
  using const_reference = const value_type&;
  static void construct(reference /*storage*/, const_reference /*value*/) {}
  static void destruct(reference storage)
  {
    storage.clear();
  }

  static reference get(reference storage)
  {
    return storage;
  }

  static const_reference get(const_reference storage)
  {
    // NOLINTNEXTLINE(bugprone-return-const-ref-from-parameter) Ignore this lint issue for this test
    return storage;
  }
};

struct OptionalIntPolicy
{
  using storage_type = std::optional<int>;
  using value_type = storage_type;
  using reference = value_type&;
  using const_reference = const value_type&;
  static void construct(reference storage, int value = std::numeric_limits<int>::max())
  {
    storage = value;
  }
  static void destruct(reference storage)
  {
    storage = std::nullopt;
  }

  static reference get(reference storage)
  {
    return storage;
  }

  static const_reference get(const_reference storage)
  {
    // NOLINTNEXTLINE(bugprone-return-const-ref-from-parameter) Ignore this lint issue for this test
    return storage;
  }
};

TEST_CASE("Concepts")
{
  STATIC_REQUIRE(std::ranges::random_access_range<CircularBuffer<jewels::memory::ObjectPolicy<int>>>);
}

TEST_CASE("Test default iterator construction")
{
  using Iterator = CircularBuffer<jewels::memory::ObjectPolicy<int>>::iterator;
  const Iterator begin{};
  const Iterator end{};
  REQUIRE(begin == end);
}

TEST_CASE("Circular position")
{
  constexpr auto size{2L};
  REQUIRE(CircularPosition{} == CircularPosition{});

  SECTION("position")
  {
    REQUIRE(CircularPosition{0L, 0U}.position() == 0L);
    REQUIRE(CircularPosition{1L, 0U}.position() == 1L);
    REQUIRE(CircularPosition{0L, 1U}.position() == 0L);
    REQUIRE(CircularPosition{1L, 1U}.position() == 1L);
  }

  SECTION("increment")
  {
    auto increment = jewels::meta::Overloaded{
      [](auto&& position, auto max_size)
      {
        position.increment(max_size);
        return position;
      },
      [](auto&& position, auto num_positions, auto max_size)
      {
        position.increment(num_positions, max_size);
        return position;
      }};

    REQUIRE(increment(CircularPosition{0L, 0U}, size) == CircularPosition{1L, 0U});
    REQUIRE(increment(CircularPosition{1L, 0U}, size) == CircularPosition{0L, 1U});
    REQUIRE(increment(CircularPosition{1L, 255U}, size) == CircularPosition{0L, 0U});

    REQUIRE(increment(CircularPosition{0L, 0U}, 0L, size) == CircularPosition{0L, 0U});
    REQUIRE(increment(CircularPosition{1L, 0U}, 0L, size) == CircularPosition{1L, 0U});
    REQUIRE(increment(CircularPosition{1L, 255U}, 0L, size) == CircularPosition{1L, 255U});

    REQUIRE(increment(CircularPosition{0L, 0U}, 1L, size) == CircularPosition{1L, 0U});
    REQUIRE(increment(CircularPosition{1L, 0U}, 1L, size) == CircularPosition{0L, 1U});
    REQUIRE(increment(CircularPosition{1L, 255U}, 1L, size) == CircularPosition{0L, 0U});

    REQUIRE(increment(CircularPosition{0L, 0U}, 2L, size) == CircularPosition{0L, 1U});
    REQUIRE(increment(CircularPosition{1L, 0U}, 2L, size) == CircularPosition{1L, 1U});
    REQUIRE(increment(CircularPosition{1L, 255U}, 2L, size) == CircularPosition{1L, 0U});
  }

  SECTION("decrement")
  {
    auto decrement = jewels::meta::Overloaded{
      [](auto&& position, auto max_size)
      {
        position.decrement(max_size);
        return position;
      },
      [](auto&& position, auto num_positions, auto max_size)
      {
        position.decrement(num_positions, max_size);
        return position;
      }};

    REQUIRE(CircularPosition{0L, 0U} == decrement(CircularPosition{1L, 0U}, size));
    REQUIRE(CircularPosition{1L, 0U} == decrement(CircularPosition{0L, 1U}, size));
    REQUIRE(CircularPosition{1L, 255U} == decrement(CircularPosition{0L, 0U}, size));

    REQUIRE(CircularPosition{0L, 0U} == decrement(CircularPosition{0L, 0U}, 0L, size));
    REQUIRE(CircularPosition{1L, 0U} == decrement(CircularPosition{1L, 0U}, 0L, size));
    REQUIRE(CircularPosition{1L, 255U} == decrement(CircularPosition{1L, 255U}, 0L, size));

    REQUIRE(CircularPosition{0L, 0U} == decrement(CircularPosition{1L, 0U}, 1L, size));
    REQUIRE(CircularPosition{1L, 0U} == decrement(CircularPosition{0L, 1U}, 1L, size));
    REQUIRE(CircularPosition{1L, 255U} == decrement(CircularPosition{0L, 0U}, 1L, size));

    REQUIRE(CircularPosition{1L, 255U} == decrement(CircularPosition{1L, 0U}, 2L, size));
    REQUIRE(CircularPosition{0L, 0U} == decrement(CircularPosition{0L, 1U}, 2L, size));
    REQUIRE(CircularPosition{0L, 255U} == decrement(CircularPosition{0L, 0U}, 2L, size));
  }

  SECTION("distance_to")
  {
    REQUIRE(CircularPosition{0L, 0U}.distance_to(CircularPosition{0L, 0U}, size) == 0L);
    REQUIRE(CircularPosition{0L, 1U}.distance_to(CircularPosition{0L, 0U}, size) == -size);
    REQUIRE(CircularPosition{0L, 0U}.distance_to(CircularPosition{0L, 1U}, size) == size);
  }

  SECTION("at_max_delta_from")
  {
    REQUIRE_FALSE(CircularPosition{0L, 0U}.at_max_delta_from(CircularPosition{0L, 0U}));
    REQUIRE_FALSE(CircularPosition{0L, 0U}.at_max_delta_from(CircularPosition{1L, 0U}));
    REQUIRE(CircularPosition{0L, 0U}.at_max_delta_from(CircularPosition{0L, 1U}));
    REQUIRE(CircularPosition{0L, 1U}.at_max_delta_from(CircularPosition{0L, 0U}));
  }
}

TEST_CASE("Test circular buffer | construction")
{
  static_assert(!std::is_copy_constructible_v<CircularBuffer<jewels::memory::ObjectPolicy<int>>>);
  static_assert(!std::is_copy_assignable_v<CircularBuffer<jewels::memory::ObjectPolicy<int>>>);
  static_assert(std::is_move_constructible_v<CircularBuffer<jewels::memory::ObjectPolicy<int>>>);
  static_assert(!std::is_move_assignable_v<CircularBuffer<jewels::memory::ObjectPolicy<int>>>);

  SECTION("From container")
  {
    REQUIRE(
      CircularBuffer<jewels::memory::ObjectPolicy<int>>::try_make(
        std::pmr::vector<jewels::memory::AlignedStorage<int>>(0)) ==
      jewels::unexpected{CircularBufferConstructError::empty_storage});
    auto buffer = CircularBuffer<jewels::memory::ObjectPolicy<int>>::try_make(
      std::pmr::vector<jewels::memory::AlignedStorage<int>>(1));
    REQUIRE(buffer);
    buffer->emplace_back(8);
    REQUIRE(*std::begin(*buffer) == 8);
  }

  SECTION("From container with state")
  {
    REQUIRE(
      CircularBuffer<OptionalIntPolicy>::try_make(
        std::pmr::vector<std::optional<int>>(3),
        clockwork::Tappy<CircularBufferState>{clockwork::TapInit<clockwork::Tachyon<CircularBufferState>>{
          .offset = 3,
          .size = 4,
        }}) == jewels::unexpected{CircularBufferConstructError::invalid_state_offset});
    REQUIRE(
      CircularBuffer<OptionalIntPolicy>::try_make(
        std::pmr::vector<std::optional<int>>(3),
        clockwork::Tappy<CircularBufferState>{clockwork::TapInit<clockwork::Tachyon<CircularBufferState>>{
          .offset = 2,
          .size = 4,
        }}) == jewels::unexpected{CircularBufferConstructError::invalid_state_size});
    SECTION("Empty with non-zero offset")
    {
      std::array<std::optional<int>, 3U> storage{0, 1, 2};
      const clockwork::Tappy<CircularBufferState> state{clockwork::TapInit<clockwork::Tachyon<CircularBufferState>>{
        .offset = 2,
        .size = 0,
      }};
      auto buffer =
        CircularBuffer<OptionalIntPolicy, std::span<std::optional<int>>>::try_make(std::span{storage}, state);
      REQUIRE(buffer);
      REQUIRE(buffer->state() == state);
      REQUIRE(buffer->empty());
      REQUIRE(storage[2] == 2);
      buffer->emplace_back(8);
      REQUIRE(storage[2] == 8);
      REQUIRE(*std::begin(*buffer) == 8);
      REQUIRE(
        buffer->state() ==
        clockwork::Tappy<CircularBufferState>{clockwork::TapInit<clockwork::Tachyon<CircularBufferState>>{
          .offset = 2,
          .size = 1,
        }});
    }
    SECTION("Not empty")
    {
      std::array<std::optional<int>, 3U> storage{0, 1, 2};
      const clockwork::Tappy<CircularBufferState> state{clockwork::TapInit<clockwork::Tachyon<CircularBufferState>>{
        .offset = 2,
        .size = 2,
      }};
      auto buffer =
        CircularBuffer<OptionalIntPolicy, std::span<std::optional<int>>>::try_make(std::span{storage}, state);
      REQUIRE(buffer);
      REQUIRE(buffer->state() == state);
      REQUIRE(std::ranges::equal(*buffer, std::array{2, 0}));
      buffer->emplace_back(8);
      REQUIRE(std::ranges::equal(*buffer, std::array{2, 0, 8}));
      REQUIRE(std::ranges::equal(storage, std::array<std::optional<int>, 3U>{0, 8, 2}));
      REQUIRE(
        buffer->state() ==
        clockwork::Tappy<CircularBufferState>{clockwork::TapInit<clockwork::Tachyon<CircularBufferState>>{
          .offset = 2,
          .size = 3,
        }});
    }
    SECTION("Full")
    {
      std::array<std::optional<int>, 3U> storage{0, 1, 2};
      const clockwork::Tappy<CircularBufferState> state{clockwork::TapInit<clockwork::Tachyon<CircularBufferState>>{
        .offset = 2,
        .size = 3,
      }};
      auto buffer =
        CircularBuffer<OptionalIntPolicy, std::span<std::optional<int>>>::try_make(std::span{storage}, state);
      REQUIRE(buffer);
      REQUIRE(buffer->state() == state);
      REQUIRE(std::ranges::equal(*buffer, std::array{2, 0, 1}));
      REQUIRE(storage[1] == 1);
      buffer->force_emplace_back(8);
      REQUIRE(std::ranges::equal(*buffer, std::array{0, 1, 8}));
      REQUIRE(std::ranges::equal(storage, std::array<std::optional<int>, 3U>{0, 1, 8}));
      REQUIRE(
        buffer->state() ==
        clockwork::Tappy<CircularBufferState>{clockwork::TapInit<clockwork::Tachyon<CircularBufferState>>{
          .offset = 0,
          .size = 3,
        }});
    }
  }

  SECTION("From resource")
  {
    REQUIRE(!CircularBuffer<jewels::memory::ObjectPolicy<int>>::try_make(
      0, jewels::memory::MemoryResource{std::pmr::new_delete_resource()}));
    auto buffer = CircularBuffer<jewels::memory::ObjectPolicy<int>>::try_make(
      1, jewels::memory::MemoryResource{std::pmr::new_delete_resource()});
    REQUIRE(buffer);
    buffer->emplace_back(8);
    REQUIRE(*std::begin(*buffer) == 8);
  }

  SECTION("Move constructor")
  {
    auto buffer = CircularBuffer<jewels::memory::ObjectPolicy<std::vector<int>>>::try_make(
      1, jewels::memory::MemoryResource{std::pmr::new_delete_resource()});
    REQUIRE(buffer);
    buffer->emplace_back(std::vector<int>{1});
    auto new_buffer{*std::move(buffer)};
    REQUIRE(buffer->empty()); // Need to make sure move constructor works NOLINT(bugprone-use-after-move,
                              // hicpp-invalid-access-moved)
    REQUIRE(!new_buffer.empty());
    REQUIRE(new_buffer.begin()->size() == 1U);
    REQUIRE(new_buffer.begin()->at(0U) == 1);
  }
}

TEST_CASE("Make circular buffer from constructor safe container")
{
  constexpr auto size{1U};
  std::array<jewels::memory::AlignedStorage<int>, size> storage{};
  CircularBuffer<jewels::memory::ObjectPolicy<int>, std::span<jewels::memory::AlignedStorage<int>, size>> buffer(
    std::in_place, storage);
  REQUIRE(buffer.empty());
  REQUIRE(!buffer.full());
  auto entry = buffer.emplace_back(8);
  REQUIRE(entry);
  REQUIRE(**entry == 8);
}

TEST_CASE("Test circular buffer | test clear")
{
  constexpr auto size{2U};
  std::pmr::vector<std::optional<int>> storage(size, std::pmr::new_delete_resource());
  /// Start a scope block so we can test clear called within the destructor as well.
  {
    auto buffer = CircularBuffer<OptionalIntPolicy, std::span<std::optional<int>>>::try_make(std::span{storage});
    REQUIRE(buffer);
    REQUIRE(buffer->empty());
    REQUIRE(!buffer->full());
    REQUIRE(buffer->size() == 0U); // NOLINT(readability-container-size-empty) Need to make sure size works too
    {
      auto entry = buffer->emplace_back();
      REQUIRE(entry);
      REQUIRE(**entry == std::numeric_limits<int>::max());
    }
    REQUIRE(!buffer->empty());
    REQUIRE(!buffer->full());
    REQUIRE(buffer->size() == 1U);
    REQUIRE(storage.at(0U) == std::numeric_limits<int>::max());
    REQUIRE(storage.at(1U) == std::nullopt);

    buffer->clear();
    REQUIRE(buffer->empty());
    REQUIRE(!buffer->full());
    REQUIRE(buffer->size() == 0U); // NOLINT(readability-container-size-empty) Need to make sure size works too
    REQUIRE(storage.at(0U) == std::nullopt);
    REQUIRE(storage.at(1U) == std::nullopt);
    {
      auto entry = buffer->emplace_back();
      REQUIRE(entry);
      REQUIRE(**entry == std::numeric_limits<int>::max());
    }
    REQUIRE(!buffer->empty());
    REQUIRE(!buffer->full());
    REQUIRE(buffer->size() == 1U);
    {
      auto entry = buffer->emplace_back();
      REQUIRE(entry);
      REQUIRE(**entry == std::numeric_limits<int>::max());
    }
    REQUIRE(!buffer->empty());
    REQUIRE(buffer->full());
    REQUIRE(buffer->size() == 2U);
    REQUIRE(storage.at(0U) == std::numeric_limits<int>::max());
    REQUIRE(storage.at(1U) == std::numeric_limits<int>::max());

    buffer->clear();
    REQUIRE(buffer->empty());
    REQUIRE(!buffer->full());
    REQUIRE(buffer->size() == 0U); // NOLINT(readability-container-size-empty) Need to make sure size works too
    REQUIRE(storage.at(0U) == std::nullopt);
    REQUIRE(storage.at(1U) == std::nullopt);

    {
      auto entry = buffer->emplace_back();
      REQUIRE(entry);
      REQUIRE(**entry == std::numeric_limits<int>::max());
    }
    REQUIRE(!buffer->empty());
    REQUIRE(!buffer->full());
    REQUIRE(buffer->size() == 1U);
    {
      auto entry = buffer->emplace_back();
      REQUIRE(entry);
      REQUIRE(**entry == std::numeric_limits<int>::max());
    }
    REQUIRE(!buffer->empty());
    REQUIRE(buffer->full());
    REQUIRE(buffer->size() == 2U);
    REQUIRE(storage.at(0U) == std::numeric_limits<int>::max());
    REQUIRE(storage.at(1U) == std::numeric_limits<int>::max());
  }

  // Destructor should be called to clear it.
  REQUIRE(storage.at(0U) == std::nullopt);
  REQUIRE(storage.at(1U) == std::nullopt);
}

TEST_CASE("Test circular buffer | test construct / destruct")
{
  constexpr auto size{2U};
  std::pmr::vector<std::optional<int>> storage(size, std::pmr::new_delete_resource());
  auto buffer = CircularBuffer<OptionalIntPolicy, std::span<std::optional<int>>>::try_make(std::span{storage});
  REQUIRE(buffer);
  {
    auto entry = buffer->emplace_back();
    REQUIRE(entry);
    REQUIRE(**entry == std::numeric_limits<int>::max());
  }
  REQUIRE(storage.at(0U) == std::numeric_limits<int>::max());
  REQUIRE(storage.at(1U) == std::nullopt);

  {
    auto entry = buffer->emplace_back();
    REQUIRE(entry);
    REQUIRE(**entry == std::numeric_limits<int>::max());
  }
  REQUIRE(storage.at(0U) == std::numeric_limits<int>::max());
  REQUIRE(storage.at(1U) == std::numeric_limits<int>::max());

  REQUIRE(buffer->pop_back());
  REQUIRE(storage.at(0U) == std::numeric_limits<int>::max());
  REQUIRE(storage.at(1U) == std::nullopt);

  REQUIRE(buffer->pop_back());
  REQUIRE(storage.at(0U) == std::nullopt);
  REQUIRE(storage.at(1U) == std::nullopt);

  {
    auto entry = buffer->emplace_front();
    REQUIRE(entry);
    REQUIRE(**entry == std::numeric_limits<int>::max());
  }
  REQUIRE(storage.at(0U) == std::nullopt);
  REQUIRE(storage.at(1U) == std::numeric_limits<int>::max());

  {
    auto entry = buffer->emplace_front();
    REQUIRE(entry);
    REQUIRE(**entry == std::numeric_limits<int>::max());
  }
  REQUIRE(storage.at(0U) == std::numeric_limits<int>::max());
  REQUIRE(storage.at(1U) == std::numeric_limits<int>::max());

  REQUIRE(buffer->pop_front());
  REQUIRE(storage.at(0U) == std::nullopt);
  REQUIRE(storage.at(1U) == std::numeric_limits<int>::max());

  REQUIRE(buffer->pop_front());
  REQUIRE(storage.at(0U) == std::nullopt);
  REQUIRE(storage.at(1U) == std::nullopt);
}

TEST_CASE("Test circular buffer | test iteration")
{
  constexpr auto size{2U};
  std::pmr::vector<std::optional<int>> storage(size, std::pmr::new_delete_resource());
  auto buffer = CircularBuffer<OptionalIntPolicy, std::span<std::optional<int>>>::try_make(std::span{storage});
  REQUIRE(buffer);

  // One
  REQUIRE(buffer->emplace_back());
  REQUIRE(buffer->size() == 1U);
  REQUIRE(buffer->begin() != buffer->end());
  REQUIRE(buffer->end() - buffer->begin() == 1);
  REQUIRE(buffer->begin() - buffer->end() == -1);
  REQUIRE(buffer->begin() + 1 == buffer->end());
  REQUIRE(++buffer->begin() == buffer->end());
  REQUIRE(buffer->begin() == buffer->end() - 1);
  REQUIRE(buffer->begin() == --buffer->end());

  // Two
  REQUIRE(buffer->emplace_back());
  REQUIRE(buffer->size() == 2U);
  REQUIRE(buffer->begin() != buffer->end());
  REQUIRE(buffer->end() - buffer->begin() == 2);
  REQUIRE(buffer->begin() - buffer->end() == -2);
  REQUIRE(buffer->begin() == buffer->end() - 2);
  REQUIRE(buffer->begin() + 1 == buffer->end() - 1);
  REQUIRE(buffer->begin() + 2 == buffer->end());
  REQUIRE(buffer->begin() == -- --buffer->end());
  REQUIRE(++buffer->begin() == --buffer->end());
  REQUIRE(++ ++buffer->begin() == buffer->end());

  // One (offset)
  REQUIRE(buffer->pop_front());
  REQUIRE(buffer->size() == 1U);
  REQUIRE(buffer->begin() != buffer->end());
  REQUIRE(buffer->end() - buffer->begin() == 1);
  REQUIRE(buffer->begin() - buffer->end() == -1);
  REQUIRE(buffer->begin() + 1 == buffer->end());
  REQUIRE(++buffer->begin() == buffer->end());
  REQUIRE(buffer->begin() == buffer->end() - 1);
  REQUIRE(buffer->begin() == --buffer->end());

  // Two (wrapped)
  REQUIRE(buffer->emplace_back());
  REQUIRE(buffer->size() == 2U);
  REQUIRE(buffer->begin() != buffer->end());
  REQUIRE(buffer->end() - buffer->begin() == 2);
  REQUIRE(buffer->begin() - buffer->end() == -2);
  REQUIRE(buffer->begin() == buffer->end() - 2);
  REQUIRE(buffer->begin() + 1 == buffer->end() - 1);
  REQUIRE(buffer->begin() + 2 == buffer->end());
  REQUIRE(buffer->begin() == -- --buffer->end());
  REQUIRE(++buffer->begin() == --buffer->end());
  REQUIRE(++ ++buffer->begin() == buffer->end());

  // One
  REQUIRE(buffer->pop_front());
  REQUIRE(buffer->size() == 1U);
  REQUIRE(buffer->begin() != buffer->end());
  REQUIRE(buffer->end() - buffer->begin() == 1);
  REQUIRE(buffer->begin() - buffer->end() == -1);
  REQUIRE(buffer->begin() + 1 == buffer->end());
  REQUIRE(++buffer->begin() == buffer->end());
  REQUIRE(buffer->begin() == buffer->end() - 1);
  REQUIRE(buffer->begin() == --buffer->end());

  // Empty
  REQUIRE(buffer->pop_front());
  REQUIRE(buffer->empty());
  REQUIRE(buffer->begin() == buffer->end());
  REQUIRE(buffer->end() - buffer->begin() == 0);
}

TEST_CASE("Const iterator for owning buffer")
{
  constexpr auto size{2U};
  const auto buffer = CircularBuffer<jewels::memory::ObjectPolicy<int>>::try_make(
    size, jewels::memory::MemoryResource{std::pmr::new_delete_resource()});
  REQUIRE(buffer);
  auto begin_iter = buffer->begin();
  auto end_iter = buffer->end();
  REQUIRE(begin_iter == end_iter);
}

TEST_CASE("Test circular buffer | overflow of wrap count")
{
  constexpr auto size{2U};
  std::pmr::vector<std::optional<int>> storage(size, std::pmr::new_delete_resource());
  auto buffer = CircularBuffer<OptionalIntPolicy, std::span<std::optional<int>>>::try_make(std::span{storage});
  REQUIRE(buffer);

  // Emplacing front from the start will underflow and result in a
  // wrap count of 255.  The end iterator will still have a wrap count
  // of 0.
  REQUIRE(buffer->emplace_front(0));
  REQUIRE(buffer->emplace_back(1));

  REQUIRE(buffer->size() == size);
  REQUIRE(!buffer->empty());
  REQUIRE(buffer->full());
  REQUIRE(buffer->end() - buffer->begin() == size);
  REQUIRE(buffer->begin() + 2 == buffer->end());
  REQUIRE(buffer->begin() == buffer->end() - 2);
}

TEST_CASE("Test circular buffer | force insertion")
{
  static constexpr auto size{2U};
  auto circular_buffer = CircularBuffer<jewels::memory::ObjectPolicy<int>>::try_make(
    size, jewels::memory::MemoryResource{std::pmr::new_delete_resource()});
  REQUIRE(circular_buffer);
  REQUIRE(circular_buffer->emplace_back(0));
  REQUIRE(circular_buffer->emplace_back(1));

  SECTION("Force back")
  {
    REQUIRE(!circular_buffer->emplace_back(2));
    auto [it, evicted] = circular_buffer->force_emplace_back(2);
    REQUIRE(it == std::prev(std::end(*circular_buffer)));
    REQUIRE(evicted);
    REQUIRE(*circular_buffer->begin() == 1);
    REQUIRE(*std::next(circular_buffer->begin()) == 2);
  }

  SECTION("Force back when not full")
  {
    REQUIRE(circular_buffer->pop_front());
    auto [it, evicted] = circular_buffer->force_emplace_back(2);
    REQUIRE(it == std::prev(std::end(*circular_buffer)));
    REQUIRE(!evicted);
    REQUIRE(*circular_buffer->begin() == 1);
    REQUIRE(*std::next(circular_buffer->begin()) == 2);
  }

  SECTION("Force front")
  {
    REQUIRE(!circular_buffer->emplace_front(2));
    auto [it, evicted] = circular_buffer->force_emplace_front(2);
    REQUIRE(it == std::begin(*circular_buffer));
    REQUIRE(evicted);
    REQUIRE(*circular_buffer->begin() == 2);
    REQUIRE(*std::next(circular_buffer->begin()) == 0);
  }

  SECTION("Force front when not full")
  {
    REQUIRE(circular_buffer->pop_back());
    auto [it, evicted] = circular_buffer->force_emplace_front(2);
    REQUIRE(it == std::begin(*circular_buffer));
    REQUIRE(!evicted);
    REQUIRE(*circular_buffer->begin() == 2);
    REQUIRE(*std::next(circular_buffer->begin()) == 0);
  }
}

TEST_CASE("Test circular buffer iterator | operator[]")
{
  static constexpr auto size{2U};
  auto circular_buffer = CircularBuffer<jewels::memory::ObjectPolicy<int>>::try_make(
    size, jewels::memory::MemoryResource{std::pmr::new_delete_resource()});
  REQUIRE(circular_buffer);
  REQUIRE(circular_buffer->emplace_back(0));
  REQUIRE(circular_buffer->emplace_back(1));

  const auto begin = std::begin(*circular_buffer);
  REQUIRE(begin[0] == 0);
  REQUIRE(begin[1] == 1);
  const auto next = std::next(begin);
  REQUIRE(next[0] == 1);
  REQUIRE(next[-1] == 0);
}

struct AliveFlagElement
{
  int value;
  bool alive;
};

struct AliveFlagPolicy
{
  using storage_type = AliveFlagElement;
  using value_type = storage_type;
  using reference = value_type&;
  using const_reference = const value_type&;
  static void construct(storage_type& storage)
  {
    storage.alive = true;
  }
  static void destruct(storage_type& storage)
  {
    storage.alive = false;
  }
  static reference get(reference storage)
  {
    return storage;
  }
  static const_reference get(const_reference storage)
  {
    // NOLINTNEXTLINE(bugprone-return-const-ref-from-parameter) Ignore this lint issue for this test
    return storage;
  }
};

/// Helper function to verify the correct elements are active and visible in the circular buffer.
/// @param storage A span over the underlying storage.
/// @param buffer A reference to the circular buffer.
/// @param offset The number of places the beginning iterator is offset from the first element.
/// @param expected_size The expected size of the circular buffer.
/// @return True if the elements in the buffer are correct and false otherwise.
bool check_elements(
  std::span<const AliveFlagElement> storage,
  const CircularBuffer<AliveFlagPolicy, std::span<AliveFlagElement>>& buffer,
  size_t offset,
  size_t expected_size)
{
  std::vector<int> values(storage.size());
  std::iota(std::begin(values), std::end(values), 0);

  // Rotate elements to match the circular buffer.
  std::ranges::rotate(values, std::next(std::begin(values), static_cast<int>(offset)));

  // Make sure all elements in the circular buffer are marked as alive.
  auto alive = std::ranges::all_of(buffer, [](auto elem) { return elem.alive; });
  if (!alive)
  {
    UNSCOPED_INFO("Elements are not alive.");
    return false;
  }

  auto alive_expected = std::span{values}.subspan(0, expected_size);

  // Check the sequence of values in the circular buffer.
  auto alive_equal = std::ranges::equal(buffer, alive_expected, {}, [](auto elem) { return elem.value; });
  if (!alive_equal)
  {
    UNSCOPED_INFO("Alive elements are not the expected sequence.");
    return false;
  }

  // If the elements in the buffer are alive, then validating the number dead in the underlying storage is sufficient.
  auto dead_count =
    static_cast<size_t>(std::count_if(std::begin(storage), std::end(storage), [](auto elem) { return !elem.alive; }));
  if (dead_count != (storage.size() - expected_size))
  {
    UNSCOPED_INFO("Unexpected dead element count: " << dead_count);
    return false;
  }
  return true;
}

/// Calculate the element offset relative to the start of the container.
/// @param storage_size The size of the underlying storage.
/// @param go_forward True if the beginning of the container is incrementing forward during the test.
/// @param emplace_back True if the container is growing from the back.
/// @param start_position The starting position of the beginning of the buffer.
/// @param current_size The current size.
size_t calculate_element_offset(
  size_t storage_size, bool go_forward, bool emplace_back, size_t start_position, size_t current_size)
{
  auto position = start_position;
  if (!go_forward)
  {
    position = storage_size - position;
  }

  // Need to account for the container growing with both emplace_back and emplace_front.
  if (emplace_back)
  {
    return position;
  }

  if (position < current_size)
  {
    return storage_size + position - current_size;
  }
  return position - current_size;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) Consolidated to avoid repeated setup.
TEST_CASE("Test circular buffer | all permutations")
{
  static constexpr auto size{4U};
  std::pmr::vector<AliveFlagElement> storage(std::pmr::new_delete_resource());
  storage.reserve(size);
  for (auto index = 0U; index < size; ++index)
  {
    storage.emplace_back(AliveFlagElement{.value = static_cast<int>(index), .alive = false});
  }
  auto buffer = CircularBuffer<AliveFlagPolicy, std::span<AliveFlagElement>>::try_make(std::span{storage});
  REQUIRE(buffer);

  // We need to stress test the internal state of the circular buffer
  // which means we need to test when wrap overflows in either
  // direction.  We also need to test when iterators have differnt
  // wrap counts.  At the end of each inner loop, an element will be
  // emplaced at one side and then popped from the other to make the
  // internal starting position move by one.  This will be tested both
  // moving the starting position forward (emplace back and pop front)
  // and moving the starting position backward (emplace front and pop
  // back).  Thus we will move the starting position forward `size()`
  // positions twice to test wrap around.  Then twice back to test it
  // in reverse.  This brings the container to it's original state.
  // Then we'll go back twice more to test the underflow.  However, we
  // have to make sure we test overflow.  So we will do two more
  // rounds forward to return to the original position and then
  // another two rounds forward to finally test everything works.
  constexpr std::array directions{true, true, false, false, false, false, true, true, true, true};
  for (auto go_forward : directions)
  {
    // At the end of each loop, the start position will move either
    // forward one or backward one.  This is done with a sequence of
    // emplace_back + pop_front or emplace_front + pop_back.  By
    // repeating this twice for each direction, we will test the wrap
    // value on both sides of 0 without having to repeat this 255
    // times to wrap all the way around.
    for (auto start_position = 0U; start_position < size; ++start_position)
    {
      REQUIRE(buffer->empty());

      auto check = [&storage, &buffer](auto element_offset, auto current_size)
      { return check_elements(storage, *buffer, element_offset, current_size); };

      auto element_offset = [go_forward, start_position](auto emplace_back, auto current_size)
      { return calculate_element_offset(size, go_forward, emplace_back, start_position, current_size); };

      // Begin stays put.

      // Build up size with emplace_back.
      for (auto current_size = 0U; current_size < size; ++current_size)
      {
        REQUIRE(check(element_offset(true, current_size), current_size));
        REQUIRE(buffer->size() == current_size);
        REQUIRE(buffer->end() - buffer->begin() == static_cast<std::ptrdiff_t>(current_size));
        REQUIRE(buffer->begin() - buffer->end() == -static_cast<std::ptrdiff_t>(current_size));
        REQUIRE(buffer->begin() + static_cast<std::ptrdiff_t>(current_size) == buffer->end());
        REQUIRE(buffer->end() - static_cast<std::ptrdiff_t>(current_size) == buffer->begin());

        REQUIRE(buffer->emplace_back());
        REQUIRE(buffer->size() == current_size + 1);
      }
      REQUIRE(buffer->full());

      // Tear down size with pop_back.
      for (auto current_size = size; current_size > 0; --current_size)
      {
        REQUIRE(check(element_offset(true, current_size), current_size));
        REQUIRE(buffer->size() == current_size);
        REQUIRE(buffer->end() - buffer->begin() == static_cast<std::ptrdiff_t>(current_size));
        REQUIRE(buffer->begin() - buffer->end() == -static_cast<std::ptrdiff_t>(current_size));
        REQUIRE(buffer->begin() + static_cast<std::ptrdiff_t>(current_size) == buffer->end());
        REQUIRE(buffer->end() - static_cast<std::ptrdiff_t>(current_size) == buffer->begin());

        REQUIRE(buffer->pop_back());
        REQUIRE(buffer->size() == current_size - 1);
      }
      REQUIRE(buffer->empty());

      // End stays put.

      // Build up size with emplace_front.
      for (auto current_size = 0U; current_size < size; ++current_size)
      {
        REQUIRE(check(element_offset(false, current_size), current_size));
        REQUIRE(buffer->size() == current_size);
        REQUIRE(buffer->end() - buffer->begin() == static_cast<std::ptrdiff_t>(current_size));
        REQUIRE(buffer->begin() - buffer->end() == -static_cast<std::ptrdiff_t>(current_size));
        REQUIRE(buffer->begin() + static_cast<std::ptrdiff_t>(current_size) == buffer->end());
        REQUIRE(buffer->end() - static_cast<std::ptrdiff_t>(current_size) == buffer->begin());

        REQUIRE(buffer->emplace_front());
        REQUIRE(buffer->size() == current_size + 1);
      }
      REQUIRE(buffer->full());

      // Tear down size with pop_front.
      for (auto current_size = size; current_size > 0; --current_size)
      {
        REQUIRE(check(element_offset(false, current_size), current_size));
        REQUIRE(buffer->size() == current_size);
        REQUIRE(buffer->end() - buffer->begin() == static_cast<std::ptrdiff_t>(current_size));
        REQUIRE(buffer->begin() - buffer->end() == -static_cast<std::ptrdiff_t>(current_size));
        REQUIRE(buffer->begin() + static_cast<std::ptrdiff_t>(current_size) == buffer->end());
        REQUIRE(buffer->end() - static_cast<std::ptrdiff_t>(current_size) == buffer->begin());

        REQUIRE(buffer->pop_front());
        REQUIRE(buffer->size() == current_size - 1);
      }
      REQUIRE(buffer->empty());

      // Begin stays put on build, but moves on teardown.

      // Build up size with emplace_back.
      for (auto current_size = 0U; current_size < size; ++current_size)
      {
        REQUIRE(check(element_offset(true, current_size), current_size));
        REQUIRE(buffer->size() == current_size);
        REQUIRE(buffer->end() - buffer->begin() == static_cast<std::ptrdiff_t>(current_size));
        REQUIRE(buffer->begin() - buffer->end() == -static_cast<std::ptrdiff_t>(current_size));
        REQUIRE(buffer->begin() + static_cast<std::ptrdiff_t>(current_size) == buffer->end());
        REQUIRE(buffer->end() - static_cast<std::ptrdiff_t>(current_size) == buffer->begin());

        REQUIRE(buffer->emplace_back());
        REQUIRE(buffer->size() == current_size + 1);
      }
      REQUIRE(buffer->full());

      // Tear down size with pop_front.
      for (auto current_size = size; current_size > 0; --current_size)
      {
        REQUIRE(check(element_offset(false, current_size), current_size));
        REQUIRE(buffer->size() == current_size);
        REQUIRE(buffer->end() - buffer->begin() == static_cast<std::ptrdiff_t>(current_size));
        REQUIRE(buffer->begin() - buffer->end() == -static_cast<std::ptrdiff_t>(current_size));
        REQUIRE(buffer->begin() + static_cast<std::ptrdiff_t>(current_size) == buffer->end());
        REQUIRE(buffer->end() - static_cast<std::ptrdiff_t>(current_size) == buffer->begin());

        REQUIRE(buffer->pop_front());
        REQUIRE(buffer->size() == current_size - 1);
      }
      REQUIRE(buffer->empty());

      // Begin stays moves on build, but stays put on tear down.

      // Build up size with emplace_back.
      for (auto current_size = 0U; current_size < size; ++current_size)
      {
        REQUIRE(check(element_offset(false, current_size), current_size));
        REQUIRE(buffer->size() == current_size);
        REQUIRE(buffer->end() - buffer->begin() == static_cast<std::ptrdiff_t>(current_size));
        REQUIRE(buffer->begin() - buffer->end() == -static_cast<std::ptrdiff_t>(current_size));
        REQUIRE(buffer->begin() + static_cast<std::ptrdiff_t>(current_size) == buffer->end());
        REQUIRE(buffer->end() - static_cast<std::ptrdiff_t>(current_size) == buffer->begin());

        REQUIRE(buffer->emplace_front());
        REQUIRE(buffer->size() == current_size + 1);
      }
      REQUIRE(buffer->full());

      // Tear down size with pop_front.
      for (auto current_size = size; current_size > 0; --current_size)
      {
        REQUIRE(check(element_offset(true, current_size), current_size));
        REQUIRE(buffer->size() == current_size);
        REQUIRE(buffer->end() - buffer->begin() == static_cast<std::ptrdiff_t>(current_size));
        REQUIRE(buffer->begin() - buffer->end() == -static_cast<std::ptrdiff_t>(current_size));
        REQUIRE(buffer->begin() + static_cast<std::ptrdiff_t>(current_size) == buffer->end());
        REQUIRE(buffer->end() - static_cast<std::ptrdiff_t>(current_size) == buffer->begin());

        REQUIRE(buffer->pop_back());
        REQUIRE(buffer->size() == current_size - 1);
      }
      REQUIRE(buffer->empty());

      if (go_forward)
      {
        // Move starting position one place forward.
        REQUIRE(buffer->emplace_back());
        REQUIRE(!buffer->empty());
        REQUIRE(buffer->pop_front());
        REQUIRE(buffer->empty());
      }
      else
      {
        // Move starting position one place backward.
        REQUIRE(buffer->emplace_front());
        REQUIRE(!buffer->empty());
        REQUIRE(buffer->pop_back());
        REQUIRE(buffer->empty());
      }
    }
  }
}

TEST_CASE("Examples from readme")
{
  constexpr size_t storage_size{8U};
  SECTION("ObjectPolicy")
  {
    using Policy = jewels::memory::ObjectPolicy<int>;
    std::pmr::vector<typename Policy::storage_type> storage{};
    storage.resize(storage_size);
    using CircBuffView = jewels::container::CircularBuffer<Policy, std::span<typename Policy::storage_type>>;
    // This returns an expected type.  See class documentation on preconditions.
    auto circ_buff = CircBuffView::try_make(std::span(storage));
    REQUIRE(circ_buff);

    // Add an element at the back.
    circ_buff->emplace_back(1);
    // Add an element at the front.
    circ_buff->emplace_front(2);
    // Access front element
    std::cout << *std::begin(*circ_buff) << "\n"; // Prints 2
    // Access back element
    std::cout << *std::prev(std::end(*circ_buff)) << "\n"; // Prints 1
    // Check the size
    std::cout << circ_buff->size() << "\n"; // Prints 2
    // Remove from back
    circ_buff->pop_back();
    // Remove from front
    circ_buff->pop_front();
    // Check if empty
    std::cout << circ_buff->empty() << "\n"; // Prints true
  }
  SECTION("ClearPolicy")
  {
    using Policy = ClearPolicy<std::pmr::vector<int>>;
    // Notice now the storage element type is a vector instead of an aligned storage.
    std::pmr::vector<std::pmr::vector<int>> storage{};
    storage.resize(storage_size);
    constexpr size_t reserve_size{8U};
    for (auto& elem : storage)
    {
      // Reserve memory ahead of time for each element.
      // This will be retained as `ClearPolicy` only clears the container when the element is removed.
      elem.reserve(reserve_size);
    }
    using CircBuffView = jewels::container::CircularBuffer<Policy, std::span<typename Policy::storage_type>>;
    auto circ_buff = CircBuffView::try_make(std::span(storage));
    REQUIRE(circ_buff);
  }
  SECTION("State")
  {
    using Policy = jewels::memory::ObjectPolicy<int>;
    std::pmr::vector<typename Policy::storage_type> storage{};
    storage.resize(storage_size);
    using CircBuffView = jewels::container::CircularBuffer<Policy, std::span<typename Policy::storage_type>>;
    // This returns an expected type.  See class documentation on preconditions.
    auto circ_buff = CircBuffView::try_make(std::span(storage));
    REQUIRE(circ_buff);

    // add / remove some elements
    circ_buff->emplace_back();
    circ_buff->emplace_back();
    circ_buff->pop_front();

    // Creates a new circular buffer pointing to identical storage with the same position state.
    // These two containers would compare equal.
    auto storage_copy = storage;
    auto resumed_circ_buff = CircBuffView::try_make(std::span(storage_copy), circ_buff->state());
    REQUIRE(resumed_circ_buff);
  }
}

} // namespace jewels::container::testing
