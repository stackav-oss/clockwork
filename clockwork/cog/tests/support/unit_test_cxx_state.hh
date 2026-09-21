// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "jewels/memory/memory_resource.hh"

#include <cstdint>

namespace clockwork::cogs::testing
{

struct CxxState
{
  explicit CxxState(jewels::memory::MemoryResource /*memres*/) {}

  int32_t value{};

  auto operator<=>(const CxxState&) const = default;
};

} // namespace clockwork::cogs::testing
