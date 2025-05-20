
// IWYU pragma: private, include "jewels/memory/pmr_shared_ptr.hh"
#pragma once

#include "jewels/memory/pmr_shared_ptr.hh"

#include "jewels/memory/memory_resource.hh"

#include <memory>
#include <memory_resource>
#include <utility>

namespace jewels::memory
{

template <typename T, typename... Args>
auto make_pmr_shared(jewels::memory::MemoryResource memres, Args&&... args)
{
  return std::allocate_shared<T>(std::pmr::polymorphic_allocator<T>{memres}, std::forward<Args>(args)...);
}

} // namespace jewels::memory
