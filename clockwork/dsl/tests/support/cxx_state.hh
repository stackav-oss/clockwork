// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "jewels/memory/memory_resource.hh"

namespace clockwork::testing
{
struct CxxState
{
  explicit CxxState(jewels::memory::MemoryResource /*memres*/) {}
};
} // namespace clockwork::testing
