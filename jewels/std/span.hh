// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <concepts>
#include <cstddef>
#include <span>
#include <type_traits>

namespace jewels
{

/// Check if a type is a span or not.  Will match a const span, but not a reference to a span.
template <class T>
concept is_span = std::same_as<std::span<typename T::element_type, T::extent>, std::remove_const_t<T>>;

/// Size of byte span.
template <class SomeSpan>
  requires(is_span<SomeSpan>)
inline constexpr auto span_byte_size{sizeof(typename SomeSpan::value_type) * SomeSpan::extent};

/// Specialization for std::dynamic_extent.
template <class T>
inline constexpr auto span_byte_size<std::span<T, std::dynamic_extent>>{std::dynamic_extent};

/// Create a span from a single value.
/// @note This allows an r-value just like `std::span{...}`.  This
/// makes it convenient to pass as an argument to a function where a
/// span is expected.  However, be careful of lifetimes when making a
/// span from a temporary.
/// @param value The single value to make a span from.
/// @return A span of size 1.
template <class Value>
std::span<std::remove_reference_t<Value>, 1U> as_single_item_span(Value&& value) noexcept;

/// Convert a span from a fixed extent to a dynamic extent.
/// @param The span to convert.
/// @return A span over the same data, but with a dynamic extent.
template <class T, auto size>
constexpr std::span<T> as_dynamic_extent(std::span<T, size> input) noexcept;

/// Convert a span to a span of const chars.
/// @param input The span to convert.
/// @return A span as const char.
template <class T, size_t size>
std::span<const char, span_byte_size<std::span<T, size>>> as_chars(std::span<T, size> input);

/// Convert a span to a span of const unsigned chars.
/// @param input The span to convert.
/// @return A span as const unsigned char.
template <class T, size_t size>
std::span<const unsigned char, span_byte_size<std::span<T, size>>> as_unsigned_chars(std::span<T, size> input);

/// Convert a span to a span of mutable unsigned chars.
/// @param input The span to convert.
/// @return A span as mutable unsigned char.
template <class T, size_t size>
  requires(!std::is_const_v<T>)
std::span<unsigned char, span_byte_size<std::span<T, size>>> as_writable_unsigned_chars(std::span<T, size> input);

} // namespace jewels

#include "jewels/std/span.inl"
