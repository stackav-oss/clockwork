// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/memory/default_memory_resource.hh"

#include "jewels/memory/memory_resource.hh"

#include <memory_resource>

namespace jewels::memory
{

jewels::memory::MemoryResource get_default_memory_resource() noexcept
{
  return jewels::memory::MemoryResource(std::pmr::get_default_resource());
}

} // namespace jewels::memory
