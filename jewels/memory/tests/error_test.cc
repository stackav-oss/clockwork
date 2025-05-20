// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/memory/error.hh"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace jewels::memory
{
TEST_CASE("enum_to_value_name")
{
  CHECK(enum_to_value_name(MemoryError::null_pointer_error) == "null_pointer_error");
  CHECK(enum_to_value_name(MemoryError::out_of_memory) == "out_of_memory");
}
} // namespace jewels::memory
