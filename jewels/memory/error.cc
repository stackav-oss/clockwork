// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/memory/error.hh"

#include <string_view>

namespace jewels::memory
{
using namespace std::literals;

std::string_view enum_to_value_name(const MemoryError error) noexcept
{
  switch (error)
  {
  case MemoryError::null_pointer_error:
    return "null_pointer_error"sv;
  case MemoryError::out_of_memory:
    return "out_of_memory"sv;
  }
  return "Unknown MemoryError - this should be unreachable"sv;
}
} // namespace jewels::memory
