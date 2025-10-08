// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <span>
#include <type_traits>

namespace jewels
{

/// Create a span from a single value.
/// @note This allows an r-value just like `std::span{...}`.  This
/// makes it convenient to pass as an argument to a function where a
/// span is expected.  However, be careful of lifetimes when making a
/// span from a temporary.
/// @param value The single value to make a span from.
/// @return A span of size 1.
template <class Value>
std::span<std::remove_reference_t<Value>, 1U> as_single_item_span(Value&& value) noexcept;

} // namespace jewels

#include "jewels/std/span.inl"
