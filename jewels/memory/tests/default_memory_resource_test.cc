// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/memory/default_memory_resource.hh"

#include <catch2/catch_test_macros.hpp>

#include <memory_resource>

namespace jewels::memory
{
TEST_CASE("get_default_memory_resource")
{
  auto memres = get_default_memory_resource();
  // The default memory resource should be the same as the one returned by the standard library.
  CHECK(static_cast<std::pmr::memory_resource*>(memres) == std::pmr::get_default_resource());
}
} // namespace jewels::memory
