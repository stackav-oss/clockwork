// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/container/tap/var_array.hh"
#include "jewels/memory/aligned_storage.hh"
#include "jewels/std/expected.hh"
#include "jewels/std/span.hh"

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <optional>
#include <ranges>
#include <span>
#include <sys/types.h>
#include <type_traits>
#include <utility>
#include <vector>

namespace jewels::tap::testing
{

struct alignas(16UL) OverAligned
{
};

template <class T, class = void>
struct PaddingSize
{
  static constexpr auto value = 0UL;
};

template <class T>
struct PaddingSize<T, std::void_t<decltype(std::declval<T>().padding)>>
{
  static constexpr auto value = sizeof(std::declval<T>().padding);
};

template <class T>
constexpr size_t field_sizes()
{
  return sizeof(std::declval<T>().storage) + sizeof(std::declval<T>().size) + PaddingSize<T>::value;
}

template <typename VarArray>
void check_tail_cleared(const VarArray& array)
{
  using Value = typename VarArray::value_type;
  if (array.size() < array.capacity())
  {
    auto tail = std::span<const Value>(
      std::next(array.data(), static_cast<ssize_t>(array.size())), array.capacity() - array.size());
    REQUIRE(std::ranges::all_of(tail, [](auto&& item) { return item == Value{}; }));
  }
}

TEST_CASE("VarArrayLayout")
{
  SECTION("Padding")
  {
    SECTION("std::byte")
    {
      REQUIRE(detail::var_array_between_padding<std::byte, 1UL>() == 7UL);
      REQUIRE(detail::var_array_trailing_padding<std::byte, 1UL>() == 0UL);

      REQUIRE(detail::var_array_between_padding<std::byte, 2UL>() == 6UL);
      REQUIRE(detail::var_array_trailing_padding<std::byte, 2UL>() == 0UL);

      REQUIRE(detail::var_array_between_padding<std::byte, 7UL>() == 1UL);
      REQUIRE(detail::var_array_trailing_padding<std::byte, 7UL>() == 0UL);

      REQUIRE(detail::var_array_between_padding<std::byte, 8UL>() == 0UL);
      REQUIRE(detail::var_array_trailing_padding<std::byte, 8UL>() == 0UL);
    }

    SECTION("uint32_t")
    {
      REQUIRE(detail::var_array_between_padding<uint32_t, 1UL>() == 4UL);
      REQUIRE(detail::var_array_trailing_padding<uint32_t, 1UL>() == 0UL);

      REQUIRE(detail::var_array_between_padding<uint32_t, 2UL>() == 0UL);
      REQUIRE(detail::var_array_trailing_padding<uint32_t, 2UL>() == 0UL);

      REQUIRE(detail::var_array_between_padding<uint32_t, 3UL>() == 4UL);
      REQUIRE(detail::var_array_trailing_padding<uint32_t, 3UL>() == 0UL);
    }

    SECTION("OverAligned")
    {
      REQUIRE(detail::var_array_between_padding<OverAligned, 1UL>() == 0UL);
      REQUIRE(detail::var_array_trailing_padding<OverAligned, 1UL>() == 8UL);

      REQUIRE(detail::var_array_between_padding<OverAligned, 2UL>() == 0UL);
      REQUIRE(detail::var_array_trailing_padding<OverAligned, 1UL>() == 8UL);

      REQUIRE(detail::var_array_between_padding<OverAligned, 3UL>() == 0UL);
      REQUIRE(detail::var_array_trailing_padding<OverAligned, 1UL>() == 8UL);
    }
  }
  SECTION("One uint32_t")
  {
    using T = detail::VarArrayLayout<uint32_t, 1UL>;
    REQUIRE(alignof(T) == 8UL);
    REQUIRE(offsetof(T, storage) == 0UL);
    REQUIRE(sizeof(std::declval<T>().storage) == 4UL);
    REQUIRE(offsetof(T, padding) == 4UL);
    REQUIRE(sizeof(std::declval<T>().padding) == 4UL);
    REQUIRE(offsetof(T, size) == 8UL);

    REQUIRE(sizeof(T) == field_sizes<T>());
  }
  SECTION("Two uint32_t")
  {
    using T = detail::VarArrayLayout<uint32_t, 2UL>;
    REQUIRE(alignof(T) == 8UL);
    REQUIRE(offsetof(T, storage) == 0UL);
    REQUIRE(sizeof(std::declval<T>().storage) == 8UL);
    REQUIRE(offsetof(T, size) == 8UL);

    REQUIRE(sizeof(T) == field_sizes<T>());
  }
  SECTION("Three uint32_t")
  {
    using T = detail::VarArrayLayout<uint32_t, 3UL>;
    REQUIRE(alignof(T) == 8UL);
    REQUIRE(offsetof(T, storage) == 0UL);
    REQUIRE(sizeof(std::declval<T>().storage) == 12UL);
    REQUIRE(offsetof(T, padding) == 12UL);
    REQUIRE(sizeof(std::declval<T>().padding) == 4UL);
    REQUIRE(offsetof(T, size) == 16UL);

    REQUIRE(sizeof(T) == field_sizes<T>());
  }
  SECTION("One OverAligned")
  {
    using T = detail::VarArrayLayout<OverAligned, 1UL>;
    REQUIRE(alignof(T) == 16UL);
    REQUIRE(offsetof(T, storage) == 0UL);
    REQUIRE(sizeof(std::declval<T>().storage) == 16UL);
    REQUIRE(offsetof(T, size) == 16UL);
    REQUIRE(offsetof(T, padding) == 24UL);
    REQUIRE(sizeof(std::declval<T>().padding) == 8UL);

    REQUIRE(sizeof(T) == field_sizes<T>());
  }
  SECTION("Two OverAligned")
  {
    using T = detail::VarArrayLayout<OverAligned, 2UL>;
    REQUIRE(alignof(T) == 16UL);
    REQUIRE(offsetof(T, storage) == 0UL);
    REQUIRE(sizeof(std::declval<T>().storage) == 32UL);
    REQUIRE(offsetof(T, size) == 32UL);
    REQUIRE(offsetof(T, padding) == 40UL);
    REQUIRE(sizeof(std::declval<T>().padding) == 8UL);

    REQUIRE(sizeof(T) == field_sizes<T>());
  }
  SECTION("Three OverAligned")
  {
    using T = detail::VarArrayLayout<OverAligned, 3UL>;
    REQUIRE(alignof(T) == 16UL);
    REQUIRE(offsetof(T, storage) == 0UL);
    REQUIRE(sizeof(std::declval<T>().storage) == 48UL);
    REQUIRE(offsetof(T, size) == 48UL);
    REQUIRE(offsetof(T, padding) == 56UL);
    REQUIRE(sizeof(std::declval<T>().padding) == 8UL);

    REQUIRE(sizeof(T) == field_sizes<T>());
  }
}

TEST_CASE("Test var array")
{
  constexpr auto capacity{10UL};
  SECTION("Default constructor")
  {
    const VarArray<uint32_t, capacity> var_array{};
    REQUIRE(var_array.empty());
  }
  SECTION("Copy constructor")
  {
    SECTION("Empty")
    {
      const VarArray<uint32_t, capacity> orig{};
      REQUIRE(orig.empty());

      auto copy{orig};
      REQUIRE(copy.empty());

      copy.emplace_back();
      REQUIRE(copy.size() == 1UL);
      REQUIRE(orig.empty());
    }

    SECTION("Not empty")
    {
      VarArray<uint32_t, capacity> orig{};
      orig.emplace_back(7U);
      REQUIRE(orig.size() == 1UL);

      auto copy{orig};
      REQUIRE(copy.size() == 1UL);
      REQUIRE(copy.at(0UL) == 7U);
    }
  }
  SECTION("Move construction")
  {
    VarArray<uint32_t, capacity> orig{1U};
    const auto copy{std::move(orig)}; // NOLINT(performance-move-const-arg) Intentionally testing move behavior
    REQUIRE(copy.size() == 1UL);
    REQUIRE(copy.at(0UL) == 1UL);
  }
  SECTION("Copy assignment")
  {
    SECTION("Empty")
    {
      const VarArray<uint32_t, capacity> orig{};
      REQUIRE(orig.empty());

      VarArray<uint32_t, capacity> copy{};
      copy = orig;
      REQUIRE(copy.empty());
    }

    SECTION("Not empty")
    {
      VarArray<uint32_t, capacity> orig{};
      orig.emplace_back(7U);
      REQUIRE(orig.size() == 1UL);

      VarArray<uint32_t, capacity> copy{};
      copy = orig;
      REQUIRE(copy.size() == 1UL);
      REQUIRE(copy.at(0UL) == 7U);

      // Assignment wipes if size shrinks.
      copy = VarArray<uint32_t, capacity>{};
      REQUIRE(copy[0] == 0U);
    }
  }
  SECTION("Move assignment")
  {
    VarArray<uint32_t, capacity> orig{1U};
    VarArray<uint32_t, capacity> copy{};
    copy = std::move(orig); // NOLINT(performance-move-const-arg) Intentionally testing move behavior
    REQUIRE(copy.size() == 1UL);
    REQUIRE(copy.at(0UL) == 1UL);
  }
  SECTION("Aggregate constructor")
  {
    SECTION("One arg")
    {
      VarArray<uint32_t, capacity> var_array{7U};
      REQUIRE(var_array.size() == 1UL);
      REQUIRE(var_array.at(0UL) == 7U);
    }
    SECTION("Multiple args")
    {
      VarArray<uint32_t, capacity> var_array{7U, 11U, 143278U};
      REQUIRE(var_array.size() == 3UL);
      REQUIRE(var_array.at(0UL) == 7U);
      REQUIRE(var_array.at(1UL) == 11U);
      REQUIRE(var_array.at(2UL) == 143278U);
    }
  }

  SECTION("Element access")
  {
    VarArray<uint32_t, capacity> var_array{7U, 11U, 143278U};
    REQUIRE(var_array.size() == 3UL);
    REQUIRE(var_array.at(0UL) == 7U);
    REQUIRE(var_array.at(1UL) == 11U);
    REQUIRE(var_array.at(2UL) == 143278U);

    REQUIRE(var_array[0UL] == 7U);
    REQUIRE(var_array[1UL] == 11U);
    REQUIRE(var_array[2UL] == 143278U);

    SECTION("Out of bounds")
    {
      var_array.pop_back();

      REQUIRE_THROWS(var_array.at(2UL));
      // No bounds checking.  This should pick up a zero because
      // popped elements get zero'd out.
      REQUIRE(var_array[2UL] == 0U);
    }
  }

  SECTION("pop_back")
  {
    VarArray<uint32_t, capacity> var_array{7U, 11U, 143278U};
    SECTION("Unsafe")
    {
      REQUIRE(var_array[2U] == 143278U);
      var_array.pop_back();
      REQUIRE(var_array[2U] == 0U);
      REQUIRE(var_array[1U] == 11U);
      var_array.pop_back();
      REQUIRE(var_array[1U] == 0U);
      REQUIRE(var_array[0U] == 7U);
      var_array.pop_back();
      REQUIRE(var_array[0U] == 0U);
      REQUIRE_THROWS(var_array.pop_back());
    }
    SECTION("Safe")
    {
      REQUIRE(var_array[2U] == 143278U);
      REQUIRE(var_array.try_pop_back());
      REQUIRE(var_array[2U] == 0U);
      REQUIRE(var_array[1U] == 11U);
      REQUIRE(var_array.try_pop_back());
      REQUIRE(var_array[1U] == 0U);
      REQUIRE(var_array[0U] == 7U);
      REQUIRE(var_array.try_pop_back());
      REQUIRE(var_array[0U] == 0U);
      REQUIRE(!var_array.try_pop_back());
    }
  }

  SECTION("Iteration")
  {
    std::array expected{7U, 11U, 143278U};
    VarArray<uint32_t, capacity> var_array{7U, 11U, 143278U};
    REQUIRE(std::ranges::equal(expected, var_array));
    REQUIRE(std::ranges::equal(expected, std::as_const(var_array)));
    REQUIRE(std::ranges::equal(std::ranges::views::reverse(expected), std::ranges::views::reverse(var_array)));
    REQUIRE(
      std::ranges::equal(std::ranges::views::reverse(expected), std::ranges::views::reverse(std::as_const(var_array))));
  }

  SECTION("Size related methods")
  {
    VarArray<uint32_t, capacity> var_array{};
    REQUIRE(var_array.empty());
    REQUIRE(!var_array.full());
    // NOLINTNEXTLINE(readability-container-size-empty) intentionally checking the behavior of size()
    REQUIRE(var_array.size() == 0UL);

    REQUIRE(var_array.capacity() == capacity);

    for (auto i = 1U; i < capacity; ++i)
    {
      var_array.emplace_back(i);
      REQUIRE(!var_array.empty());
      REQUIRE(!var_array.full());
      REQUIRE(var_array.size() == i);
    }
    var_array.emplace_back(static_cast<uint32_t>(capacity));
    REQUIRE(!var_array.empty());
    REQUIRE(var_array.full());
    REQUIRE(var_array.size() == capacity);

    for (auto i = 1U; i < capacity; ++i)
    {
      var_array.pop_back();
      REQUIRE(!var_array.empty());
      REQUIRE(!var_array.full());
      REQUIRE(var_array.size() == (capacity - i));
    }
    var_array.pop_back();
    REQUIRE(var_array.empty());
    REQUIRE(!var_array.full());
    // NOLINTNEXTLINE(readability-container-size-empty) intentionally checking the behavior of size()
    REQUIRE(var_array.size() == 0UL);
  }

  SECTION("Appending elements")
  {
    VarArray<uint32_t, capacity> var_array{};
    SECTION("emplace_back")
    {
      for (auto i = 0U; i < capacity; ++i)
      {
        auto& elem = var_array.emplace_back(i);
        REQUIRE(elem == i);
        elem = i + 1;
        REQUIRE(std::ranges::equal(var_array, std::ranges::views::iota(1U, i + 2)));
      }
      REQUIRE_THROWS(var_array.emplace_back(static_cast<uint32_t>(capacity)));
    }
    SECTION("try_emplace_back")
    {
      for (auto i = 0U; i < capacity; ++i)
      {
        auto elem = var_array.try_emplace_back(i);
        REQUIRE(elem);
        REQUIRE(**elem == i);
        **elem = i + 1;
        REQUIRE(std::ranges::equal(var_array, std::ranges::views::iota(1U, i + 2)));
      }
      REQUIRE(var_array.try_emplace_back(static_cast<uint32_t>(capacity)) == jewels::unexpected{jewels::MonoError{}});
    }
    SECTION("push_back")
    {
      for (auto i = 0U; i < capacity; ++i)
      {
        var_array.push_back(i);
        REQUIRE(std::ranges::equal(var_array, std::ranges::views::iota(0U, i + 1)));
      }
      REQUIRE_THROWS(var_array.push_back(static_cast<uint32_t>(capacity)));
    }
  }

  SECTION("re-setting")
  {
    VarArray<uint32_t, capacity> var_array{};
    var_array.push_back(1U);
    var_array.push_back(2U);
    SECTION("try_set increase")
    {
      std::vector<uint32_t> vec{{4, 5, 6}};
      REQUIRE(var_array.try_set(vec));
      REQUIRE(std::ranges::equal(var_array, vec));
    }
    SECTION("try_set decrease")
    {
      var_array.push_back(3U);
      std::vector<uint32_t> vec{{4, 5}};
      REQUIRE(var_array.try_set(vec));
      REQUIRE(std::ranges::equal(var_array, vec));
      REQUIRE(var_array[var_array.size()] == uint32_t{});
    }
    SECTION("try_set oversize")
    {
      std::vector<uint32_t> vec(capacity + 1);
      REQUIRE(!var_array.try_set(vec));
      REQUIRE(std::ranges::equal(var_array, std::ranges::views::iota(1U, 3U)));
    }
    check_tail_cleared(var_array);
  }

  SECTION("clear")
  {
    VarArray<uint32_t, capacity> var_array{};
    REQUIRE(var_array.empty());
    var_array.clear();
    REQUIRE(var_array.empty());
    var_array.emplace_back(0U);
    REQUIRE(!var_array.empty());
    var_array.clear();
    REQUIRE(var_array.empty());
    for (auto i = 0U; i < capacity; ++i)
    {
      var_array.emplace_back(i);
    }
    for (auto i = 0U; i < capacity; ++i)
    {
      REQUIRE(var_array.at(i) == i);
    }
    REQUIRE(var_array.full());
    var_array.clear();
    REQUIRE(var_array.empty());
    for (auto i = 0U; i < capacity; ++i)
    {
      REQUIRE(var_array[i] == 0U);
    }
    REQUIRE(
      std::ranges::all_of(
        as_bytes(jewels::as_single_item_span(var_array)), [](auto byte) { return byte == std::byte{}; }));
  }

  SECTION("reserve")
  {
    VarArray<uint32_t, capacity> var_array{};
    for (auto i = 0U; i < capacity; ++i)
    {
      var_array.reserve(i);
      var_array.emplace_back(i);
    }
    var_array.reserve(capacity);
    REQUIRE_THROWS(var_array.reserve(capacity + 1UL));
  }

  SECTION("resize")
  {
    SECTION("Default value")
    {
      VarArray<uint32_t, capacity> var_array{};
      REQUIRE(var_array.empty());
      var_array.resize(5UL);
      REQUIRE(std::ranges::count(var_array, 0UL) == 5UL);
      var_array.at(3) = 3U;
      var_array.at(4) = 4U;
      // Change values prior to resizing to validate zeroing.
      REQUIRE(var_array[3] == 3U);
      REQUIRE(var_array[4] == 4U);
      var_array.resize(3UL);
      REQUIRE(var_array[3] == 0U);
      REQUIRE(var_array[4] == 0U);
      REQUIRE(std::ranges::count(var_array, 0UL) == 3UL);
      var_array.resize(capacity);
      REQUIRE(std::ranges::count(var_array, 0UL) == capacity);
      REQUIRE_THROWS(var_array.resize(capacity + 1UL));
    }
    SECTION("Specified value")
    {
      VarArray<uint32_t, capacity> var_array{};
      REQUIRE(var_array.empty());
      var_array.resize(5UL, 7UL);
      REQUIRE(std::ranges::count(var_array, 7UL) == 5UL);
      REQUIRE(var_array[2] == 7U);
      REQUIRE(var_array[3] == 7U);
      REQUIRE(var_array[4] == 7U);
      var_array.resize(2L);
      REQUIRE(var_array[2] == 0U);
      REQUIRE(var_array[3] == 0U);
      REQUIRE(var_array[4] == 0U);
      REQUIRE(std::ranges::count(var_array, 7UL) == 2UL);
      var_array.resize(capacity, 8UL);
      REQUIRE(std::ranges::count(std::ranges::views::take(var_array, 2UL), 7UL) == 2UL);
      REQUIRE(std::ranges::count(std::ranges::views::drop(var_array, 2UL), 8UL) == 8UL);
      REQUIRE_THROWS(var_array.resize(capacity + 1UL, 8UL));
    }
  }

  SECTION("constexpr")
  {
    constexpr VarArray<uint32_t, capacity> var_array{};
    STATIC_REQUIRE(var_array.empty());
    STATIC_REQUIRE(!var_array.full());
    STATIC_REQUIRE(var_array.capacity() == capacity);
  }

  SECTION("from bytes")
  {
    using T = VarArray<uint32_t, capacity>;
    jewels::memory::AlignedStorage<T> storage{};
    std::ranges::fill(storage.bytes, std::byte{255U});
    auto check_zero = [](auto byte) { return byte == std::byte{}; };
    REQUIRE(std::ranges::all_of(storage.bytes, std::not_fn(check_zero)));
    jewels::memory::ObjectPolicy<T>::construct(storage);
    REQUIRE(std::ranges::all_of(storage.bytes, check_zero));

    auto& var_array = jewels::memory::ObjectPolicy<T>::get(storage);
    for (auto i = 0U; i < capacity; ++i)
    {
      var_array.emplace_back(i + 1U);
    }
    REQUIRE(!std::ranges::all_of(storage.bytes, check_zero));
    var_array.clear();
    REQUIRE(std::ranges::all_of(storage.bytes, check_zero));
  }

  SECTION("equality")
  {
    // NOLINTNEXTLINE(readability-container-size-empty) intentionally checking equality operator
    REQUIRE(VarArray<int32_t, 3>{} == VarArray<int32_t, 3>{});
    REQUIRE(VarArray<int32_t, 3>{1} == VarArray<int32_t, 3>{1});
    REQUIRE(VarArray<int32_t, 3>{1, 2} == VarArray<int32_t, 3>{1, 2});
    REQUIRE(VarArray<int32_t, 3>{1, 2} != VarArray<int32_t, 3>{1, 3});
    REQUIRE(VarArray<int32_t, 3>{1, 2} != VarArray<int32_t, 3>{1});
    REQUIRE(VarArray<int32_t, 3>{1, 3} != VarArray<int32_t, 3>{1, 2});
    REQUIRE(VarArray<int32_t, 3>{1} != VarArray<int32_t, 3>{1, 2});
  }

  SECTION("Nested")
  {
    VarArray<VarArray<int32_t, 2>, 2> var_array{VarArray<int32_t, 2>{}};
    var_array.emplace_back(VarArray<int32_t, 2>{3});
    REQUIRE(var_array.full());
    REQUIRE(var_array.at(0UL).empty());
    REQUIRE(var_array.at(1UL).size() == 1UL);
    REQUIRE(var_array.at(1UL).at(0UL) == 3);
  }
}

TEST_CASE("VarArray callsig operations")
{
  VarArray<uint32_t, 2UL> array{};

  uint32_t* value{nullptr};
  REQUIRE(jewels::fails(array.at(jewels::Out{value}, 0UL)));
  REQUIRE(value == nullptr);

  REQUIRE(jewels::ok(array.try_reserve(2UL)));
  REQUIRE(jewels::fails(array.try_reserve(3UL)));
  REQUIRE(jewels::fails(array.try_resize(3UL)));
  REQUIRE(array.empty());

  REQUIRE(jewels::ok(array.try_resize(1UL, 7U)));
  REQUIRE(array.size() == 1UL);
  REQUIRE(jewels::ok(array.at(jewels::Out{value}, 0UL)));
  REQUIRE(*value == 7U);

  uint32_t* inserted{nullptr};
  REQUIRE(jewels::ok(array.try_emplace_back(jewels::OptionalOut{inserted}, 9U)));
  REQUIRE(inserted == &array[1]);
  REQUIRE(*inserted == 9U);
  REQUIRE(jewels::fails(array.try_emplace_back(jewels::OptionalOut<uint32_t*>{std::nullopt}, 11U)));
  REQUIRE(array.size() == 2UL);

  REQUIRE(jewels::ok(array.try_pop_back(callsig)));
  REQUIRE(jewels::ok(array.try_pop_back(callsig)));
  REQUIRE(jewels::fails(array.try_pop_back(callsig)));

  std::array values{1U, 2U};
  REQUIRE(jewels::ok(array.try_set(values, callsig)));
  REQUIRE(jewels::fails(array.try_set(std::array<uint32_t, 3UL>{1U, 2U, 3U}, callsig)));
  REQUIRE(array.size() == 2UL);

  REQUIRE(jewels::ok(array.try_resize(1UL)));
  VarArray<uint32_t, 2UL>::iterator position{array.begin()};
  REQUIRE(jewels::ok(array.try_insert(jewels::Out{position}, array.begin(), 3U)));
  REQUIRE(position == array.begin());
  REQUIRE(array[0] == 3U);
  REQUIRE(jewels::fails(array.try_insert(jewels::Out{position}, array.begin(), 4U)));

  array.clear();
  REQUIRE(jewels::ok(array.try_emplace_back(jewels::OptionalOut<uint32_t*>{std::nullopt}, 1U)));
  std::array additional_values{2U};
  REQUIRE(
    jewels::ok(
      array.try_insert(jewels::Out{position}, array.end(), additional_values.begin(), additional_values.end())));
  REQUIRE(position == std::prev(array.end()));
  REQUIRE(array[1] == 2U);

  const auto unchanged_position = position;
  REQUIRE(
    jewels::fails(
      array.try_insert(jewels::Out{position}, array.end(), additional_values.begin(), additional_values.end())));
  REQUIRE(position == unchanged_position);
}

// Use both VarArray and std::vector to make sure we comply with the
// same interface.  In particular which iterator gets returned.
TEMPLATE_TEST_CASE("Erase", "", std::vector<uint32_t>, (VarArray<uint32_t, 3UL>))
{
  SECTION("Single erase")
  {
    SECTION("Mutable iterators")
    {
      SECTION("One element")
      {
        TestType container{};
        container.emplace_back(1U);
        REQUIRE(container.erase(std::begin(container)) == std::end(container));
        REQUIRE(container.empty());
      }
      SECTION("Multiple")
      {
        TestType container{};
        container.emplace_back(1U);
        container.emplace_back(2U);
        container.emplace_back(3U);
        SECTION("First")
        {
          REQUIRE(container.erase(std::begin(container)) == std::begin(container));
          REQUIRE(std::ranges::equal(container, std::array{2U, 3U}));
        }
        SECTION("Middle")
        {
          REQUIRE(container.erase(std::next(std::begin(container))) == std::next(std::begin(container)));
          REQUIRE(std::ranges::equal(container, std::array{1U, 3U}));
        }
        SECTION("Last")
        {
          REQUIRE(container.erase(std::prev(std::end(container))) == std::end(container));
          REQUIRE(std::ranges::equal(container, std::array{1U, 2U}));
        }
      }
    }
  }

  SECTION("Range erase")
  {
    SECTION("Mutable iterators")
    {
      SECTION("One element")
      {
        TestType container{};
        container.emplace_back(1U);
        REQUIRE(container.erase(std::begin(container), std::end(container)) == std::end(container));
        REQUIRE(container.empty());
      }
      SECTION("Multiple")
      {
        TestType container{};
        container.emplace_back(1U);
        container.emplace_back(2U);
        container.emplace_back(3U);
        SECTION("First")
        {
          REQUIRE(container.erase(std::begin(container), std::next(std::begin(container))) == std::begin(container));
          REQUIRE(std::ranges::equal(container, std::array{2U, 3U}));
        }
        SECTION("Middle")
        {
          REQUIRE(
            container.erase(std::next(std::begin(container)), std::next(std::begin(container), 2L)) ==
            std::next(std::begin(container)));
          REQUIRE(std::ranges::equal(container, std::array{1U, 3U}));
        }
        SECTION("Last")
        {
          REQUIRE(
            container.erase(std::next(std::begin(container), 2L), std::next(std::begin(container), 3L)) ==
            std::next(std::begin(container), 2L));
          REQUIRE(std::ranges::equal(container, std::array{1U, 2U}));
        }
        SECTION("First two")
        {
          REQUIRE(
            container.erase(std::begin(container), std::next(std::begin(container), 2L)) == std::begin(container));
          REQUIRE(std::ranges::equal(container, std::array{3U}));
        }
        SECTION("Last two")
        {
          REQUIRE(
            container.erase(std::next(std::begin(container)), std::next(std::begin(container), 3L)) ==
            std::next(std::begin(container)));
          REQUIRE(std::ranges::equal(container, std::array{1U}));
        }
        SECTION("All")
        {
          REQUIRE(container.erase(std::begin(container), std::end(container)) == std::begin(container));
          REQUIRE(container.empty());
        }
      }
    }
  }
}

// Use both VarArray and std::vector to make sure we comply with the
// same interface.  In particular which iterator gets returned.
TEMPLATE_TEST_CASE("Insert", "", std::vector<uint32_t>, (VarArray<uint32_t, 8UL>))
{
  TestType container{};
  SECTION("Insert single element")
  {
    REQUIRE(container.insert(std::begin(container), 2U) == std::begin(container));
    REQUIRE(std::ranges::equal(container, std::array{2U}));

    // At the front.
    REQUIRE(container.insert(std::begin(container), 1U) == std::begin(container));
    REQUIRE(std::ranges::equal(container, std::array{1U, 2U}));

    // At the back.
    REQUIRE(container.insert(std::end(container), 4U) == std::prev(std::end(container)));
    REQUIRE(std::ranges::equal(container, std::array{1U, 2U, 4U}));

    // In the middle.
    REQUIRE(container.insert(std::prev(std::end(container)), 3U) == std::prev(std::end(container), 2L));
    REQUIRE(std::ranges::equal(container, std::array{1U, 2U, 3U, 4U}));
  }
  SECTION("Insert range")
  {
    SECTION("No elements")
    {
      constexpr std::array<uint32_t, 0UL> to_insert{};
      REQUIRE(
        container.insert(std::begin(container), std::begin(to_insert), std::end(to_insert)) == std::begin(container));
      REQUIRE(container.insert(std::end(container), std::begin(to_insert), std::end(to_insert)) == std::end(container));
      container.emplace_back(1U);
      container.emplace_back(2U);
      REQUIRE(
        container.insert(std::begin(container), std::begin(to_insert), std::end(to_insert)) == std::begin(container));
      REQUIRE(container.insert(std::end(container), std::begin(to_insert), std::end(to_insert)) == std::end(container));
      REQUIRE(
        container.insert(std::next(std::begin(container)), std::begin(to_insert), std::end(to_insert)) ==
        std::next(std::begin(container)));
    }
    SECTION("Single element")
    {
      {
        constexpr std::array to_insert{2U};
        REQUIRE(
          container.insert(std::begin(container), std::begin(to_insert), std::end(to_insert)) == std::begin(container));
        REQUIRE(std::ranges::equal(container, std::array{2U}));
      }
      {
        constexpr std::array to_insert{1U};
        // At the front.
        REQUIRE(
          container.insert(std::begin(container), std::begin(to_insert), std::end(to_insert)) == std::begin(container));
        REQUIRE(std::ranges::equal(container, std::array{1U, 2U}));
      }
      {
        constexpr std::array to_insert{4U};
        // At the back.
        REQUIRE(
          container.insert(std::end(container), std::begin(to_insert), std::end(to_insert)) ==
          std::prev(std::end(container)));
        REQUIRE(std::ranges::equal(container, std::array{1U, 2U, 4U}));
      }
      {
        constexpr std::array to_insert{3U};
        // In the middle.
        REQUIRE(
          container.insert(std::next(std::begin(container), 2L), std::begin(to_insert), std::end(to_insert)) ==
          std::prev(std::end(container), 2L));
        REQUIRE(std::ranges::equal(container, std::array{1U, 2U, 3U, 4U}));
      }
    }
    SECTION("Multiple elements")
    {
      {
        constexpr std::array to_insert{3U, 6U};
        REQUIRE(
          container.insert(std::begin(container), std::begin(to_insert), std::end(to_insert)) == std::begin(container));
        REQUIRE(std::ranges::equal(container, std::array{3U, 6U}));
      }
      {
        constexpr std::array to_insert{1U, 2U};
        // At the front.
        REQUIRE(
          container.insert(std::begin(container), std::begin(to_insert), std::end(to_insert)) == std::begin(container));
        REQUIRE(std::ranges::equal(container, std::array{1U, 2U, 3U, 6U}));
      }
      {
        constexpr std::array to_insert{7U, 8U};
        // At the back.
        REQUIRE(
          container.insert(std::end(container), std::begin(to_insert), std::end(to_insert)) ==
          std::prev(std::end(container), 2L));
        REQUIRE(std::ranges::equal(container, std::array{1U, 2U, 3U, 6U, 7U, 8U}));
      }
      {
        constexpr std::array to_insert{4U, 5U};
        // In the middle.
        REQUIRE(
          container.insert(std::next(std::begin(container), 3L), std::begin(to_insert), std::end(to_insert)) ==
          std::prev(std::end(container), 5L));
        REQUIRE(std::ranges::equal(container, std::array{1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U}));
      }
    }
  }
}

TEST_CASE("Insert throws")
{
  VarArray<uint32_t, 3UL> var_array{};
  SECTION("Single element")
  {
    constexpr std::array to_insert{1U, 2U, 3U};
    var_array.insert(std::end(var_array), std::begin(to_insert), std::end(to_insert));
    REQUIRE_THROWS(var_array.insert(std::end(var_array), 4U));
  }
  SECTION("Range")
  {
    SECTION("When empty")
    {
      constexpr std::array to_insert{1U, 2U, 3U, 4U};
      REQUIRE_THROWS(var_array.insert(std::end(var_array), std::begin(to_insert), std::end(to_insert)));
    }
    SECTION("When not empty")
    {
      var_array.emplace_back(1U);
      constexpr std::array to_insert{2U, 3U, 4U};
      REQUIRE_THROWS(var_array.insert(std::end(var_array), std::begin(to_insert), std::end(to_insert)));
    }
  }
}

} // namespace jewels::tap::testing
