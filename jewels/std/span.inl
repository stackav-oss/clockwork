// IWYU pragma: private, include "jewels/std/span.hh"
#pragma once

#include "jewels/std/span.hh"

#include <cstddef>
#include <span>
#include <type_traits>

namespace jewels
{

template <class Value>
// NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward) Forward not needed for span constructor.
std::span<std::remove_reference_t<Value>, 1U> as_single_item_span(Value&& value) noexcept
{
  return std::span<std::remove_reference_t<Value>, 1U>{&value, 1U};
}

template <class T, auto size>
constexpr std::span<T> as_dynamic_extent(std::span<T, size> input) noexcept
{
  return input.subspan(0U, std::dynamic_extent);
}

template <class T, size_t size>
std::span<const char, span_byte_size<std::span<T, size>>> as_chars(std::span<T, size> input)
{
  return std::span<const char, span_byte_size<std::span<T, size>>>{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) char can alias any type.
    reinterpret_cast<const char*>(input.data()),
    input.size_bytes()};
}

template <class T, size_t size>
std::span<const unsigned char, span_byte_size<std::span<T, size>>> as_unsigned_chars(std::span<T, size> input)
{
  return std::span<const unsigned char, span_byte_size<std::span<T, size>>>{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) unsigned char can alias any type.
    reinterpret_cast<const unsigned char*>(input.data()),
    input.size_bytes()};
}

template <class T, size_t size>
  requires(!std::is_const_v<T>)
std::span<unsigned char, span_byte_size<std::span<T, size>>> as_writable_unsigned_chars(std::span<T, size> input)
{
  return std::span<unsigned char, span_byte_size<std::span<T, size>>>{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) unsigned char can alias any type.
    reinterpret_cast<unsigned char*>(input.data()),
    input.size_bytes()};
}

} // namespace jewels
