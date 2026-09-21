// IWYU pragma: private, include "jewels/memory/allocator_deleter.hh"

#pragma once

#include "jewels/memory/allocator_deleter.hh"

#include <memory>
#include <new>
#include <type_traits>
#include <utility>

template <typename Alloc>
constexpr jewels::memory::detail::BaseDeleter<Alloc>::BaseDeleter(const Alloc& alloc)
  : alloc_(alloc)
{
}

template <typename Alloc>
constexpr auto jewels::memory::detail::BaseDeleter<Alloc>::operator=(BaseDeleter&& other) noexcept(
  std::is_nothrow_move_constructible_v<BaseDeleter>) -> BaseDeleter&
{
  // self-move protection
  if (this == std::addressof(other))
  {
    return *this;
  }

  this->~BaseDeleter();
  ::new (this) BaseDeleter(std::move(other));
  return *this;
}

template <typename Alloc>
constexpr Alloc jewels::memory::detail::BaseDeleter<Alloc>::get_allocator() const
{
  return alloc_;
}

template <typename Alloc>
constexpr void jewels::memory::MonomorphicDeleter<Alloc>::operator()(pointer ptr) const
{
  auto alloc = this->get_allocator();

  AllocTraits::destroy(alloc, ptr);
  AllocTraits::deallocate(alloc, ptr, 1);
}

template <typename Alloc>
template <typename OtherAlloc, typename>
constexpr jewels::memory::PolymorphicDeleter<Alloc>::PolymorphicDeleter(const PolymorphicDeleter<OtherAlloc>& other)
  : detail::BaseDeleter<Alloc>::BaseDeleter(other.get_allocator()), deleter_(other.get_deleter())
{
}

template <typename Alloc>
constexpr jewels::memory::PolymorphicDeleter<Alloc>::PolymorphicDeleter(const MonomorphicDeleter<Alloc>& other)
  : detail::BaseDeleter<Alloc>::BaseDeleter(other.get_allocator())
{
}

template <typename Alloc>
constexpr auto jewels::memory::PolymorphicDeleter<Alloc>::get_deleter() const -> Deleter
{
  return deleter_;
}

template <typename Alloc>
void jewels::memory::PolymorphicDeleter<Alloc>::operator()(pointer ptr)
{
  static_assert(std::is_pointer_v<pointer>, "Fancy pointers are not supported");

  // Invalidate deleter state to detect potential violation of invariants.
  const auto old_deleter = std::exchange(deleter_, nullptr);

  // An invariant has been violated if the deleter is null at the point of deletion. This indicates a logic error such
  // as double deletion or reset without reassigning deleter state.
  if (old_deleter == nullptr)
  {
    // If this is reached from the destructor of std::unique_ptr, it will result in termination.
    throw BadPolymorphicDeletion();
  }

  // Casting through void pointer is guaranteed correct by the constraints of PolymorphicDeleter converting constructor,
  // which ensure that adjustment is not required for the pointer conversion. The erased deleter can thus directly cast
  // to the expected pointer type without further adjustment.
  old_deleter(this->get_allocator(), ptr);
}

template <typename Alloc>
constexpr void jewels::memory::PolymorphicDeleter<Alloc>::do_delete(const VoidAlloc& void_alloc, VoidPointer void_ptr)
{
  Alloc alloc = void_alloc;
  // Offset adjustment is prevented by substitution failure, so here we can directly cast to the expected pointer type
  // without further adjustment.
  const auto ptr = static_cast<pointer>(void_ptr);

  AllocTraits::destroy(alloc, ptr);
  AllocTraits::deallocate(alloc, ptr, 1);
}
