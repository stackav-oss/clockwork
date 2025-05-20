// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "jewels/memory/memory_resource.hh"

#include <map>
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
