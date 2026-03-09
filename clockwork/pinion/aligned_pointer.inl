// IWYU pragma: private, include "clockwork/pinion/aligned_pointer.hh"
#pragma once

#include "clockwork/pinion/aligned_pointer.hh"

#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/std/span.hh"

#include <cstddef>
#include <iterator>
#include <type_traits>

namespace clockwork::pinion
{

template <typename T, size_t alignment>
jewels::expected<AlignedPtr<T, alignment>, jewels::MonoError>
AlignedPtr<T, alignment>::try_make(jewels::memory::ObjectPtr<T> ptr) noexcept
{
  constexpr auto mask{alignment - 1U};
  if ((jewels::memory::to_uintptr_t(ptr.get()) & mask) > 0UL)
  {
    return jewels::unexpected{jewels::MonoError{}};
  }
  return AlignedPtr<T, alignment>{ptr.get()};
}

template <typename T, size_t alignment>
template <class Object>
AlignedPtr<T, alignment> AlignedPtr<T, alignment>::from_ref(Object& object) noexcept
{
  static_assert(alignof(Object) >= alignment, "Invalid alignment.");
  if constexpr (std::is_const_v<T>)
  {
    return AlignedPtr<T, alignment>(as_bytes(jewels::as_single_item_span(object)).data());
  }
  else
  {
    return AlignedPtr<T, alignment>(as_writable_bytes(jewels::as_single_item_span(object)).data());
  }
}

template <typename T, size_t alignment>
AlignedPtr<T, alignment>::AlignedPtr(const AlignedPtr<std::remove_const_t<T>, alignment>& other) noexcept
  requires std::is_const_v<T>
  : ptr_(other.get())
{
}

template <typename T, size_t alignment>
T* AlignedPtr<T, alignment>::get() const noexcept
{
  return ptr_;
}

template <typename T, size_t alignment>
T* AlignedPtr<T, alignment>::operator->() const noexcept
{
  return get();
}

template <typename T, size_t alignment>
AlignedPtr<T, alignment>& AlignedPtr<T, alignment>::operator++() noexcept
{
  *this += 1L;
  return *this;
}

template <typename T, size_t alignment>
AlignedPtr<T, alignment>& AlignedPtr<T, alignment>::operator--() noexcept
{
  *this -= 1L;
  return *this;
}

template <typename T, size_t alignment>
AlignedPtr<T, alignment> AlignedPtr<T, alignment>::operator++(int) & noexcept
{
  AlignedPtr<T, alignment> copy{*this};
  ++*this;
  return copy;
}

template <typename T, size_t alignment>
AlignedPtr<T, alignment> AlignedPtr<T, alignment>::operator--(int) & noexcept
{
  AlignedPtr<T, alignment> copy{*this};
  --*this;
  return copy;
}

template <typename T, size_t alignment>
AlignedPtr<T, alignment> AlignedPtr<T, alignment>::operator+(std::ptrdiff_t offset) const noexcept
{
  return AlignedPtr<T, alignment>(std::next(ptr_, offset * static_cast<std::ptrdiff_t>(alignment)));
}

template <typename T, size_t alignment>
AlignedPtr<T, alignment>& AlignedPtr<T, alignment>::operator+=(std::ptrdiff_t offset) noexcept
{
  *this = *this + offset;
  return *this;
}

template <typename T, size_t alignment>
AlignedPtr<T, alignment> AlignedPtr<T, alignment>::operator-(std::ptrdiff_t offset) const noexcept
{
  return *this + -offset;
}

template <typename T, size_t alignment>
AlignedPtr<T, alignment>& AlignedPtr<T, alignment>::operator-=(std::ptrdiff_t offset) noexcept
{
  *this = *this - offset;
  return *this;
}

template <typename T, size_t alignment>
bool AlignedPtr<T, alignment>::operator==(AlignedPtr<T, alignment> rhs) const noexcept
{
  return ptr_ == rhs.ptr_;
}

template <typename T, size_t alignment>
bool AlignedPtr<T, alignment>::operator!=(AlignedPtr<T, alignment> rhs) const noexcept
{
  return !(*this == rhs);
}

template <typename T, size_t alignment>
AlignedPtr<T, alignment>::AlignedPtr(T* ptr) noexcept
  : ptr_{ptr}
{
}

} // namespace clockwork::pinion
