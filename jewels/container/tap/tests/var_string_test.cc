// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/container/tap/var_array.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/memory/aligned_storage.hh"
#include "jewels/std/expected.hh"

#include <catch2/catch_test_macros.hpp>
#include <fmt10/base.h>
#include <fmt10/format.h> // IWYU pragma: keep

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <functional>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

namespace jewels::tap::testing
{

TEST_CASE("Test var array")
{
  constexpr auto capacity{10UL};
  SECTION("Default constructor")
  {
    const VarString<capacity> var_string{};
    REQUIRE(var_string.empty());
  }

  SECTION("Aggregate constructor")
  {
    SECTION("Not max")
    {
      VarString<capacity> var_string{'a', 'b', 'c'};
      REQUIRE(std::strlen(var_string.c_str()) == 3UL);
      REQUIRE(std::ranges::equal(var_string, std::string_view{"abc"}));
      REQUIRE(!var_string.full());
    }
    SECTION("Max")
    {
      VarString<capacity> var_string{'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i'};
      REQUIRE(std::strlen(var_string.c_str()) == 9UL);
      REQUIRE(std::ranges::equal(var_string, std::string_view{"abcdefghi"}));
      REQUIRE(var_string.full());
    }
    SECTION("Exceeds max")
    {
      STATIC_REQUIRE(
        !std::is_constructible_v<VarString<capacity>, char, char, char, char, char, char, char, char, char, char>);
    }
  }

  SECTION("Fixed length c-string constructor")
  {
    SECTION("Empty")
    {
      VarString<capacity> var_string{""};
      REQUIRE(var_string.empty());
      REQUIRE(std::strlen(var_string.c_str()) == 0UL);
      REQUIRE(std::ranges::equal(var_string, std::string_view{""}));
      REQUIRE(!var_string.full());
    }
    SECTION("Not max")
    {
      VarString<capacity> var_string{"abc"};
      REQUIRE(var_string.size() == 3UL);
      REQUIRE(std::strlen(var_string.c_str()) == 3UL);
      REQUIRE(std::ranges::equal(var_string, std::string_view{"abc"}));
      REQUIRE(!var_string.full());
    }
    SECTION("Max")
    {
      VarString<capacity> var_string{"abcdefghi"};
      REQUIRE(var_string.size() == 9UL);
      REQUIRE(std::strlen(var_string.c_str()) == 9UL);
      REQUIRE(std::ranges::equal(var_string, std::string_view{"abcdefghi"}));
      REQUIRE(var_string.full());
    }
    SECTION("Exceeds max")
    {
      STATIC_REQUIRE(!std::is_constructible_v<VarString<capacity>, decltype("abcdefghij")>);
    }
  }

  SECTION("Clear")
  {
    SECTION("Empty")
    {
      VarString<capacity> var_string{""};
      REQUIRE(var_string.empty());
      var_string.clear();
      REQUIRE(std::ranges::equal(var_string, std::string_view{}));
      REQUIRE(std::strlen(var_string.c_str()) == 0UL);
    }
    SECTION("Not max")
    {
      VarString<capacity> var_string{"abc"};
      REQUIRE(var_string.size() == 3UL);
      var_string.clear();
      REQUIRE(std::ranges::equal(var_string, std::string_view{}));
      REQUIRE(std::strlen(var_string.c_str()) == 0UL);
    }
    SECTION("Max")
    {
      VarString<capacity> var_string{"abcdefghi"};
      REQUIRE(var_string.size() == 9UL);
      var_string.clear();
      REQUIRE(std::ranges::equal(var_string, std::string_view{}));
      REQUIRE(std::strlen(var_string.c_str()) == 0UL);
    }
  }

  SECTION("c_str")
  {
    VarString<capacity> var_string{};
    for (auto i = 0UL; i < (capacity - 1UL); ++i)
    {
      REQUIRE(var_string.size() == i);
      REQUIRE(std::strlen(var_string.c_str()) == var_string.size());
      var_string.emplace_back(static_cast<char>(i + 1UL));
    }

    REQUIRE(var_string.try_emplace_back(char{}) == jewels::unexpected{jewels::MonoError{}});

    for (auto i = 1UL; i < capacity; ++i)
    {
      REQUIRE(std::strlen(var_string.c_str()) == capacity - i);
      var_string.pop_back();
    }
    REQUIRE(std::strlen(var_string.c_str()) == 0UL);
  }

  SECTION("try_set")
  {
    VarString<capacity> var_string;
    CHECK(var_string.try_set("123456789"));
    CHECK(var_string.string_view() == "123456789");
    CHECK(!var_string.try_set("1234567890"));
    CHECK(var_string.string_view() == "123456789");
  }

  SECTION("set_truncate")
  {
    VarString<capacity> var_string{'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x'};
    var_string.set_truncate("abc");
    REQUIRE(var_string.string_view() == "abc");
    var_string.set_truncate("abcdefghi");
    REQUIRE(var_string.string_view() == "abcdefghi");
    var_string.set_truncate("abcdefghij");
    REQUIRE(var_string.string_view() == "abcdefghi");
  }

  SECTION("string_view")
  {
    const VarString<capacity> var_string{'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i'};
    const std::string_view view = var_string;
    REQUIRE(view.size() == var_string.size());
    REQUIRE(view == "abcdefghi");
  }

  SECTION("from bytes")
  {
    using T = VarString<capacity>;
    jewels::memory::AlignedStorage<T> storage{};
    std::ranges::fill(storage.bytes, std::byte{255U});
    auto check_zero = [](auto byte) { return byte == std::byte{}; };
    REQUIRE(std::ranges::all_of(storage.bytes, std::not_fn(check_zero)));
    jewels::memory::ObjectPolicy<T>::construct(storage);
    REQUIRE(std::ranges::all_of(storage.bytes, check_zero));

    auto& var_string = jewels::memory::ObjectPolicy<T>::get(storage);
    REQUIRE(std::strlen(var_string.c_str()) == 0UL);
    for (auto i = 1U; i < capacity; ++i)
    {
      var_string.emplace_back(static_cast<char>(i));
      REQUIRE(std::strlen(var_string.c_str()) == var_string.size());
    }
    REQUIRE(!std::ranges::all_of(storage.bytes, check_zero));
    var_string.clear();
    REQUIRE(var_string.empty());
    REQUIRE(std::strlen(var_string.c_str()) == var_string.size());
    REQUIRE(std::ranges::all_of(storage.bytes, check_zero));
  }

  SECTION("equality")
  {
    REQUIRE(VarString<3>{} == VarString<3>{});
    REQUIRE(VarString<3>{'1'} == VarString<3>{'1'});
    REQUIRE(VarString<3>{'1', '2'} == VarString<3>{'1', '2'});
    REQUIRE(VarString<3>{'1', '2'} != VarString<3>{'1', '3'});
    REQUIRE(VarString<3>{'1', '2'} != VarString<3>{'1'});
    REQUIRE(VarString<3>{'1', '3'} != VarString<3>{'1', '2'});
    REQUIRE(VarString<3>{'1'} != VarString<3>{'1', '2'});
  }
  SECTION("erase")
  {
    VarString<4> var_string{'a', 'b', 'c'};
    SECTION("Single")
    {
      SECTION("Begin")
      {
        REQUIRE(var_string.erase(std::begin(var_string)) == std::begin(var_string));
        REQUIRE(std::ranges::equal(var_string, std::array{'b', 'c'}));
        REQUIRE(std::strlen(var_string.c_str()) == 2UL);
      }
      SECTION("End")
      {
        REQUIRE(var_string.erase(std::prev(std::end(var_string))) == std::end(var_string));
        REQUIRE(std::ranges::equal(var_string, std::array{'a', 'b'}));
        REQUIRE(std::strlen(var_string.c_str()) == 2UL);
      }
    }
    SECTION("Multiple")
    {
      SECTION("Begin")
      {
        REQUIRE(
          var_string.erase(std::begin(var_string), std::next(std::begin(var_string), 2UL)) == std::begin(var_string));
        REQUIRE(std::ranges::equal(var_string, std::array{'c'}));
        REQUIRE(std::strlen(var_string.c_str()) == 1UL);
      }
      SECTION("End")
      {
        REQUIRE(var_string.erase(std::prev(std::end(var_string), 2U), std::end(var_string)) == std::end(var_string));
        REQUIRE(std::ranges::equal(var_string, std::array{'a'}));
        REQUIRE(std::strlen(var_string.c_str()) == 1UL);
      }
    }
  }
  SECTION("insert")
  {
    VarString<5> var_string{};
    SECTION("Single")
    {
      REQUIRE(var_string.insert(std::begin(var_string), 'b') == std::begin(var_string));
      REQUIRE(std::ranges::equal(var_string, std::array{'b'}));

      REQUIRE(var_string.insert(std::begin(var_string), 'a') == std::begin(var_string));
      REQUIRE(std::ranges::equal(var_string, std::array{'a', 'b'}));

      REQUIRE(var_string.insert(std::end(var_string), 'c') == std::prev(std::end(var_string)));
      REQUIRE(std::ranges::equal(var_string, std::array{'a', 'b', 'c'}));
    }
    SECTION("Multiple")
    {
      {
        constexpr std::array to_insert{'a', 'b'};
        REQUIRE(
          var_string.insert(std::begin(var_string), std::begin(to_insert), std::end(to_insert)) ==
          std::begin(var_string));
        REQUIRE(std::ranges::equal(var_string, std::array{'a', 'b'}));
      }
      {
        constexpr std::array to_insert{'c', 'd'};
        REQUIRE(
          var_string.insert(std::end(var_string), std::begin(to_insert), std::end(to_insert)) ==
          std::prev(std::end(var_string), 2UL));
        REQUIRE(std::ranges::equal(var_string, std::array{'a', 'b', 'c', 'd'}));
      }
    }
  }
  SECTION("Nested")
  {
    VarArray<VarString<2>, 2> var_array{VarString<2>{'a'}};
    var_array.emplace_back(VarString<2>{});
    REQUIRE(var_array.full());
    REQUIRE(var_array.at(0UL).size() == 1UL);
    REQUIRE(var_array.at(0UL).at(0UL) == 'a');
    REQUIRE(var_array.at(1UL).empty());
  }

  SECTION("format")
  {
    const VarString<capacity> var_string{'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i'};
    std::string buffer;
    fmt::format_to(std::back_inserter(buffer), "VarString='{:>11}'", var_string);
    REQUIRE(buffer == "VarString='  abcdefghi'");
  }
}

} // namespace jewels::tap::testing
