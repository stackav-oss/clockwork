// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <span>

namespace jewels::memory
{

/// Construct a `T` from a span of bytes using memcpy.
/// @param bytes A span of bytes that represent a T.
/// @return A T with the same byte representation as the input span.
template <class T>
[[nodiscard]] T bit_cast_to(std::span<const std::byte, sizeof(T)> bytes) noexcept;

/// Write a T as bytes into a byte buffer.
/// @param value The T to write as bytes.
/// @param bytes The buffer to write to.
template <class T>
void write_as_bytes(const T& value, std::span<std::byte, sizeof(T)> bytes) noexcept;

} // namespace jewels::memory

#include "jewels/memory/bits.inl"
