// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/container/bounded_string.hh"

#include <catch2/catch_test_macros.hpp>
#include <fmt10/format.h> // IWYU pragma: keep

#include <cstring>

namespace jewels::container
{

TEST_CASE("Default bounded_string")
{
  const BoundedString<2> string;

  CHECK(string.empty());
  // size can't be checked here as it runs afoul of the linter
  CHECK(string.data()[0] == 0);
  CHECK(string.max_size() == 2);
}

TEST_CASE("Normal string")
{
  constexpr size_t size = 4;
  constexpr const char* input = "abc";
  auto string = BoundedString<size>::truncated(input);

  CHECK(!string.empty());
  CHECK(string.size() == 3);
  CHECK(string.length() == 3);
  CHECK(string.max_size() == size);
  CHECK(string.data()[0] == 'a');
  CHECK(string.data()[1] == 'b');
  CHECK(string.data()[2] == 'c');
  CHECK(string.data()[3] == 0);
  CHECK(string[0] == 'a');
  CHECK(string[1] == 'b');
  CHECK(string[2] == 'c');
  CHECK(string.data() == string.c_str());
  CHECK(strcmp(input, string.c_str()) == 0);
  CHECK(string.to_string_view() == input);
}

TEST_CASE("Truncated string")
{
  constexpr size_t size = 4;
  constexpr const char* input = "abcdef";
  const auto string = BoundedString<size>::truncated(input);

  CHECK(!string.empty());
  CHECK(string.size() == size);
  CHECK(string.length() == size);
  CHECK(string.max_size() == size);
  CHECK(string.data()[0] == 'a');
  CHECK(string.data()[1] == 'b');
  CHECK(string.data()[2] == 'c');
  CHECK(string.data()[3] == 'd');
  CHECK(string.data()[4] == 0);
  CHECK(string[0] == 'a');
  CHECK(string[1] == 'b');
  CHECK(string[2] == 'c');
  CHECK(string[3] == 'd');
  CHECK(string.data() == string.c_str());
  CHECK(strcmp(input, string.c_str()) != 0);
  CHECK(strncmp(input, string.c_str(), size) == 0);

  SECTION("assignment from smaller")
  {
    BoundedString<size + 1> bigger;
    bigger = string;
    CHECK(bigger.size() == size);
    CHECK(bigger.length() == size);
    CHECK(bigger.max_size() == size + 1);
    CHECK(bigger.to_string_view() == string.to_string_view());
    CHECK(strcmp(bigger.data(), string.data()) == 0);
  }
  SECTION("construction from smaller")
  {
    const BoundedString<size + 1> bigger(string);
    CHECK(bigger.size() == size);
    CHECK(bigger.length() == size);
    CHECK(bigger.max_size() == size + 1);
    CHECK(bigger.to_string_view() == string.to_string_view());
    CHECK(strcmp(bigger.data(), string.data()) == 0);
  }
}

TEST_CASE("Maybe construction")
{
  constexpr const char* input = "abcd";
  const auto string3 = BoundedString<3>::try_make(input);
  const auto string6 = BoundedString<6>::try_make(input);
  CHECK(!string3.has_value());
  CHECK(string6.has_value());
  CHECK(string6.value().to_string_view() == input);
}

TEST_CASE("Null termination of generalized string_view")
{
  constexpr const char* input = "abcdefg";
  constexpr const std::string_view view = std::string_view(input).substr(1, 5);
  // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage) TODO(DX-1794): Fix this
  CHECK(strcmp(view.data(), "bcdef") != 0);

  SECTION("truncated")
  {
    const auto string1 = BoundedString<10>::truncated(view);
    CHECK(string1.to_string_view() == "bcdef");
    CHECK(strcmp(string1.data(), "bcdef") == 0);

    const auto string2 = BoundedString<2>::truncated(view);
    CHECK(string2.to_string_view() == "bc");
    CHECK(strcmp(string2.data(), "bc") == 0);
  }
  SECTION("try_make")
  {
    const auto string1 = BoundedString<10>::try_make(view);
    REQUIRE(string1.has_value());
    CHECK(string1->to_string_view() == "bcdef");
    CHECK(strcmp(string1->data(), "bcdef") == 0);

    const auto string2 = BoundedString<2>::try_make(view);
    CHECK(!string2.has_value());
  }
}

TEST_CASE("concat")
{
  auto string = BoundedString<6>::truncated("abc");
  CHECK(string.to_string_view() == "abc");
  SECTION("exactly fits")
  {
    CHECK(string.try_concat("def"));
    CHECK(string.to_string_view() == "abcdef");
    CHECK(string.size() == 6);
    CHECK(string[string.size()] == 0);
  }
  SECTION("too big")
  {
    CHECK(!string.try_concat("defg"));
    CHECK(string.to_string_view() == "abc");
  }
  SECTION("repeated")
  {
    CHECK(string.try_concat("d"));
    CHECK(string.try_concat("e"));
    CHECK(string.try_concat("f"));
    CHECK(!string.try_concat("g"));
    CHECK(string.to_string_view() == "abcdef");
    CHECK(string[string.size()] == 0);
  }
}

TEST_CASE("operator==")
{
  SECTION("Same size")
  {
    SECTION("Empty")
    {
      REQUIRE(BoundedString<2>{} == BoundedString<2>{}); // NOLINT(readability-container-size-empty)
    }

    SECTION("Full")
    {
      const auto str = BoundedString<2>::truncated("ab");
      REQUIRE(str == BoundedString<2>::truncated("ab"));
      REQUIRE(str != BoundedString<2>::truncated("ba"));
    }
  }
  SECTION("Different size")
  {
    SECTION("Empty")
    {
      REQUIRE(BoundedString<2>{} == BoundedString<3>{}); // NOLINT(readability-container-size-empty)
    }
    SECTION("Full")
    {
      const auto str = BoundedString<2>::truncated("ab");
      REQUIRE(str == BoundedString<3>::truncated("ab"));
      REQUIRE(str != BoundedString<3>::truncated("abc"));
    }
  }
}

TEST_CASE("fmt compatibility")
{
  REQUIRE(fmt::format("{}", BoundedString<3>::truncated("abc")) == "abc");
}

} // namespace jewels::container
