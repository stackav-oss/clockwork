
// IWYU pragma: private, include "jewels/memory/pmr_unique_ptr.hh"
#pragma once

#include "jewels/memory/pmr_unique_ptr.hh"

#include "jewels/memory/memory_resource.hh"

#include <memory>
#include <memory_resource>
#include <optional>
#include <utility>

namespace jewels::memory
{
template <typename T>
detail::PmrDeleter<T>::PmrDeleter(jewels::memory::MemoryResource memres)
  : memres_(std::move(memres))
{
}

template <typename T>
void detail::PmrDeleter<T>::operator()(typename AllocTraits::pointer object)
{
  // Technically not required as the spec says this will never get called if object is nullptr.
  if (!memres_)
  {
    return;
  }

  AllocType allocator(*memres_);
  AllocTraits::destroy(allocator, std::to_address(object));
  AllocTraits::deallocate(allocator, object, 1);
}

template <typename T, typename... Args>
auto make_pmr_unique(jewels::memory::MemoryResource memres, Args&&... args)
{
  using AllocType = std::pmr::polymorphic_allocator<T>;
  using AllocTraits = std::allocator_traits<AllocType>;

  AllocType allocator{memres};
  typename AllocTraits::pointer obj = AllocTraits::allocate(allocator, 1);
  try
  {
    AllocTraits::construct(allocator, std::to_address(obj), std::forward<Args>(args)...);
  }
  catch (...)
  {
    AllocTraits::deallocate(allocator, obj, 1);
    throw;
  }
  return pmr_unique_ptr<T>(obj, std::move(detail::PmrDeleter<T>(memres)));
}

} // namespace jewels::memory
