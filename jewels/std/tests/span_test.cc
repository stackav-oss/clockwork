// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/std/span.hh"

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <span>
#include <utility>

namespace
{

template <class T, auto size_left, auto size_right>
bool compare_ptr_and_size(std::span<T, size_left> lhs, std::span<T, size_right> rhs)
{
  return (lhs.data() == rhs.data()) && (lhs.size() == rhs.size());
}

TEST_CASE("Smoke test")
{
  std::array arr = {1, 2, 3};
  const std::span<const int> span{arr};
  REQUIRE(span.size() == 3);
  REQUIRE(span.data() == arr.data());
  REQUIRE(std::to_address(span.begin()) == arr.data());
  REQUIRE(std::to_address(span.end()) == std::next(arr.data(), 3));
}

TEST_CASE("is_span")
{
  STATIC_REQUIRE(jewels::is_span<std::span<std::byte>>);
  STATIC_REQUIRE(jewels::is_span<std::span<const std::byte>>);
  STATIC_REQUIRE(jewels::is_span<std::span<std::byte, 8UL>>);
  STATIC_REQUIRE(jewels::is_span<std::span<const std::byte, 8UL>>);
  STATIC_REQUIRE(jewels::is_span<const std::span<std::byte, 8UL>>);
  STATIC_REQUIRE(!jewels::is_span<std::span<const std::byte, 8UL>&>);
  STATIC_REQUIRE(!jewels::is_span<int>);
}

TEST_CASE("Single span")
{
  int value{7U};
  const auto span = jewels::as_single_item_span(value);
  REQUIRE(span.size() == 1);
  REQUIRE(span.data() == &value);
  REQUIRE(span[0U] == 7);
}

TEST_CASE("As dynamic extent")
{
  STATIC_REQUIRE(
    std::same_as<decltype(jewels::as_dynamic_extent(std::declval<std::span<int, 123U>>())), std::span<int>>);
  STATIC_REQUIRE(
    std::
      same_as<decltype(jewels::as_dynamic_extent(std::declval<std::span<const int, 123U>>())), std::span<const int>>);
  STATIC_REQUIRE(std::same_as<decltype(jewels::as_dynamic_extent(std::declval<std::span<int>>())), std::span<int>>);
  STATIC_REQUIRE(
    std::same_as<decltype(jewels::as_dynamic_extent(std::declval<std::span<const int>>())), std::span<const int>>);
  std::array data{1U, 2U, 3U};
  REQUIRE(compare_ptr_and_size(std::span{data}, jewels::as_dynamic_extent(std::span{data})));
  REQUIRE(
    compare_ptr_and_size(std::span{std::as_const(data)}, jewels::as_dynamic_extent(std::span{std::as_const(data)})));
}

TEST_CASE("span_byte_size")
{
  STATIC_REQUIRE(jewels::span_byte_size<std::span<std::byte>> == std::dynamic_extent);
  STATIC_REQUIRE(jewels::span_byte_size<std::span<std::byte, 3U>> == (3U * sizeof(std::byte)));
  STATIC_REQUIRE(jewels::span_byte_size<std::span<const std::byte>> == std::dynamic_extent);
  STATIC_REQUIRE(jewels::span_byte_size<std::span<const std::byte, 3U>> == (3U * sizeof(std::byte)));

  STATIC_REQUIRE(jewels::span_byte_size<std::span<uint32_t>> == std::dynamic_extent);
  STATIC_REQUIRE(jewels::span_byte_size<std::span<uint32_t, 3U>> == 12U);
  STATIC_REQUIRE(jewels::span_byte_size<std::span<const uint32_t>> == std::dynamic_extent);
  STATIC_REQUIRE(jewels::span_byte_size<std::span<const uint32_t, 3U>> == 12U);
}

template <class Span>
concept CallChars = requires(Span span) { jewels::as_chars(span); };

TEMPLATE_TEST_CASE("as_chars", "", std::byte, uint32_t)
{
  std::array<TestType, 3U> data{};

  auto check_ptr_and_size = [&data](auto input)
  {
    const auto data_bytes = as_bytes(std::span{data});
    return (static_cast<const void*>(data_bytes.data()) == static_cast<const void*>(input.data())) &&
           (data_bytes.size() == input.size());
  };

  STATIC_REQUIRE(CallChars<std::span<TestType>>);
  STATIC_REQUIRE(CallChars<std::span<const TestType>>);
  STATIC_REQUIRE(CallChars<std::span<TestType, 1U>>);
  STATIC_REQUIRE(CallChars<std::span<const TestType, 1U>>);

  STATIC_REQUIRE(
    std::same_as<
      std::decay_t<decltype(jewels::as_chars(std::span{data}.subspan(0)))>,
      std::span<const char, std::dynamic_extent>>);
  STATIC_REQUIRE(
    std::same_as<
      std::decay_t<decltype(jewels::as_chars(std::span{std::as_const(data)}.subspan(0)))>,
      std::span<const char, std::dynamic_extent>>);
  STATIC_REQUIRE(
    std::
      same_as<std::decay_t<decltype(jewels::as_chars(std::span{data}))>, std::span<const char, 3U * sizeof(TestType)>>);
  STATIC_REQUIRE(
    std::same_as<
      std::decay_t<decltype(jewels::as_chars(std::span{std::as_const(data)}))>,
      std::span<const char, 3U * sizeof(TestType)>>);
  REQUIRE(check_ptr_and_size(jewels::as_chars(std::span{data})));
  REQUIRE(check_ptr_and_size(jewels::as_chars(std::span{std::as_const(data)})));
}

template <class Span>
concept CallUnsignedChars = requires(Span span) { jewels::as_unsigned_chars(span); };

TEMPLATE_TEST_CASE("as_unsigned_chars", "", std::byte, uint32_t)
{
  std::array<TestType, 3U> data{};

  auto check_ptr_and_size = [&data](auto input)
  { return compare_ptr_and_size(as_bytes(std::span{data}), as_bytes(input)); };

  STATIC_REQUIRE(CallUnsignedChars<std::span<TestType>>);
  STATIC_REQUIRE(CallUnsignedChars<std::span<const TestType>>);
  STATIC_REQUIRE(CallUnsignedChars<std::span<TestType, 1U>>);
  STATIC_REQUIRE(CallUnsignedChars<std::span<const TestType, 1U>>);

  STATIC_REQUIRE(
    std::same_as<
      std::decay_t<decltype(jewels::as_unsigned_chars(jewels::as_dynamic_extent(std::span{data})))>,
      std::span<const unsigned char, std::dynamic_extent>>);
  STATIC_REQUIRE(
    std::same_as<
      std::decay_t<decltype(jewels::as_unsigned_chars(jewels::as_dynamic_extent(std::span{std::as_const(data)})))>,
      std::span<const unsigned char, std::dynamic_extent>>);
  STATIC_REQUIRE(
    std::same_as<
      std::decay_t<decltype(jewels::as_unsigned_chars(std::span{data}))>,
      std::span<const unsigned char, 3U * sizeof(TestType)>>);
  STATIC_REQUIRE(
    std::same_as<
      std::decay_t<decltype(jewels::as_unsigned_chars(std::span{std::as_const(data)}))>,
      std::span<const unsigned char, 3U * sizeof(TestType)>>);
  REQUIRE(check_ptr_and_size(jewels::as_unsigned_chars(std::span{data})));
  REQUIRE(check_ptr_and_size(jewels::as_unsigned_chars(std::span{std::as_const(data)})));
}

template <class Span>
concept CallWritableUnsignedChars = requires(Span span) { jewels::as_writable_unsigned_chars(span); };

TEMPLATE_TEST_CASE("as_writable_unsigned_chars", "", std::byte, uint32_t)
{
  std::array<TestType, 3U> data{};

  auto check_ptr_and_size = [&data](auto input)
  {
    const auto data_bytes = as_bytes(std::span{data});
    return (static_cast<const void*>(data_bytes.data()) == static_cast<const void*>(input.data())) &&
           (data_bytes.size() == input.size());
  };

  STATIC_REQUIRE(CallWritableUnsignedChars<std::span<std::byte>>);
  STATIC_REQUIRE(!CallWritableUnsignedChars<std::span<const std::byte>>);
  STATIC_REQUIRE(CallWritableUnsignedChars<std::span<std::byte, 1U>>);
  STATIC_REQUIRE(!CallWritableUnsignedChars<std::span<const std::byte, 1U>>);

  STATIC_REQUIRE(
    std::same_as<
      std::decay_t<decltype(jewels::as_writable_unsigned_chars(jewels::as_dynamic_extent(std::span{data})))>,
      std::span<unsigned char, std::dynamic_extent>>);
  STATIC_REQUIRE(
    std::same_as<
      std::decay_t<decltype(jewels::as_writable_unsigned_chars(std::span{data}))>,
      std::span<unsigned char, 3U * sizeof(TestType)>>);
  REQUIRE(check_ptr_and_size(jewels::as_writable_unsigned_chars(std::span{data})));
}

} // namespace
