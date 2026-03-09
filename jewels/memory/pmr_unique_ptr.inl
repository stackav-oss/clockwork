
// IWYU pragma: private, include "jewels/memory/pmr_unique_ptr.hh"
#pragma once

#include "jewels/memory/pmr_unique_ptr.hh"

#include "jewels/memory/memory_resource.hh"

#include <memory>
#include <memory_resource>
#include <optional>
#include <type_traits>
#include <utility>

namespace jewels::memory
{
template <typename T, bool supports_polymorphic_deletion>
detail::PmrDeleter<T, supports_polymorphic_deletion>::PmrDeleter(jewels::memory::MemoryResource memres)
  : memres_(std::move(memres))
{
}

template <typename T, bool supports_polymorphic_deletion>
template <typename U>
detail::PmrDeleter<T, supports_polymorphic_deletion>::PmrDeleter(const detail::PmrDeleter<U, true>& other)
  requires supports_polymorphic_deletion && std::is_base_of_v<T, U> && std::has_virtual_destructor_v<T>
  : memres_(other.memres_)
{
  if constexpr (supports_polymorphic_deletion)
  {
    this->allocation_size_ = other.allocation_size_;
    this->alignment_ = other.alignment_;
  }
}

template <typename T, bool supports_polymorphic_deletion>
void detail::PmrDeleter<T, supports_polymorphic_deletion>::operator()(typename AllocTraits::pointer object)
{
  // Technically not required as the spec says this will never get called if object is nullptr.
  if (!memres_)
  {
    return;
  }

  AllocType allocator(*memres_);
  AllocTraits::destroy(allocator, std::to_address(object));
  if constexpr (supports_polymorphic_deletion)
  {
    // If this deleter supports polymorphic deletion, we need to deallocate with the original allocation size and
    // alignment to ensure proper deallocation of derived types through base pointers.
    allocator.deallocate_bytes(std::to_address(object), this->allocation_size_, this->alignment_);
  }
  else
  {
    AllocTraits::deallocate(allocator, object, 1);
  }
}

template <typename T, bool supports_polymorphic_deletion, typename... Args>
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
  return pmr_unique_ptr<T, supports_polymorphic_deletion>(
    obj, detail::PmrDeleter<T, supports_polymorphic_deletion>(memres));
}

template <typename T, typename... Args>
auto make_polymorphic_pmr_unique(jewels::memory::MemoryResource memres, Args&&... args)
{
  return make_pmr_unique<T, true, Args...>(memres, std::forward<Args>(args)...);
}

} // namespace jewels::memory
