
// IWYU pragma: private, include "jewels/memory/pmr_unique_ptr.hh"
#pragma once

#include "jewels/memory/pmr_unique_ptr.hh"

#include "jewels/memory/allocator_deleter.hh"

#include <memory>
#include <memory_resource>
#include <utility>

namespace jewels::memory
{

template <typename T, bool enable_polymorphic_deletion, typename... Args>
unique_ptr<T, enable_polymorphic_deletion> make_unique(Args&&... args)
{
  return memory::allocate_unique<T, enable_polymorphic_deletion>(std::allocator<T>{}, std::forward<Args>(args)...);
}

template <typename T, bool enable_polymorphic_deletion, typename Alloc, typename... Args>
unique_ptr<T, enable_polymorphic_deletion, Alloc> allocate_unique(const Alloc& alloc, Args&&... args)
{
  using AllocTraits = typename std::allocator_traits<Alloc>::template rebind_traits<T>;

  typename AllocTraits::allocator_type rebound = alloc;
  const auto ptr = AllocTraits::allocate(rebound, 1);

  try
  {
    AllocTraits::construct(rebound, ptr, std::forward<Args>(args)...);
    return {ptr, AllocatorDeleter<T, enable_polymorphic_deletion, Alloc>(rebound)};
  }
  catch (...)
  {
    AllocTraits::deallocate(rebound, ptr, 1);
    throw;
  }
}

template <typename T, bool enable_polymorphic_deletion, typename... Args>
pmr_unique_ptr<T, enable_polymorphic_deletion>
make_pmr_unique(const std::pmr::polymorphic_allocator<T>& alloc, Args&&... args)
{
  return memory::allocate_unique<T, enable_polymorphic_deletion>(alloc, std::forward<Args>(args)...);
}

template <typename T, typename... Args>
pmr_unique_ptr<T, true> make_polymorphic_pmr_unique(const std::pmr::polymorphic_allocator<T>& alloc, Args&&... args)
{
  return memory::allocate_unique<T, true>(alloc, std::forward<Args>(args)...);
}

template <typename T, typename Alloc>
std::unique_ptr<T, PolymorphicDeleter<Alloc>> to_polymorphic(std::unique_ptr<T, MonomorphicDeleter<Alloc>>&& mono_ptr)
{
  PolymorphicDeleter<Alloc> deleter(mono_ptr.get_deleter());
  return {std::move(mono_ptr).release(), deleter};
}

} // namespace jewels::memory
