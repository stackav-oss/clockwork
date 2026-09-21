// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "jewels/memory/memory_resource.hh"

#include <cstdint>
#include <functional>
#include <map>
#include <memory_resource>

namespace clockwork::testing
{
struct MyCxxFibState
{
  explicit MyCxxFibState(jewels::memory::MemoryResource memres)
    : map(memres)
  {
  }

  std::pmr::map<uint64_t, uint64_t> map;
};
} // namespace clockwork::testing
