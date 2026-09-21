// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string_view>

namespace jewels::memory
{
/// Errors originating from utilities in the memory kit.
enum class MemoryError : uint8_t
{
  /// A pointer is unexpectedly null.
  null_pointer_error,
  /// Not enough memory for an allocation.
  out_of_memory,
};

/// Get the name of an error from the value.
std::string_view enum_to_value_name(MemoryError error) noexcept;
} // namespace jewels::memory
