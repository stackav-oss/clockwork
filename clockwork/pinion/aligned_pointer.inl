// IWYU pragma: private, include "clockwork/pinion/aligned_pointer.hh"
#pragma once

#include "clockwork/pinion/aligned_pointer.hh"

#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/std/span.hh"

#include <cstddef>
#include <iterator>

namespace clockwork::pinion
{

template <size_t alignment>
jewels::expected<AlignedPtr<alignment>, jewels::MonoError>
AlignedPtr<alignment>::try_make(jewels::memory::ObjectPtr<std::byte> ptr) noexcept
{
  constexpr auto mask{alignment - 1U};
  if ((jewels::memory::to_uintptr_t(ptr.get()) & mask) > 0UL)
  {
    return jewels::unexpected{jewels::MonoError{}};
  }
  return AlignedPtr<alignment>{ptr.get()};
}

template <size_t alignment>
template <class Object>
AlignedPtr<alignment> AlignedPtr<alignment>::from_ref(Object& object) noexcept
{
  static_assert(alignof(Object) >= alignment, "Invalid alignment.");
  return AlignedPtr<alignment>(as_writable_bytes(jewels::as_single_item_span(object)).data());
}

template <size_t alignment>
std::byte* AlignedPtr<alignment>::get() const noexcept
{
  return ptr_;
}

template <size_t alignment>
std::byte* AlignedPtr<alignment>::operator->() const noexcept
{
  return get();
}

template <size_t alignment>
AlignedPtr<alignment>& AlignedPtr<alignment>::operator++() noexcept
{
  *this += 1L;
  return *this;
}

template <size_t alignment>
AlignedPtr<alignment>& AlignedPtr<alignment>::operator--() noexcept
{
  *this -= 1L;
  return *this;
}

template <size_t alignment>
AlignedPtr<alignment> AlignedPtr<alignment>::operator++(int) & noexcept
{
  AlignedPtr<alignment> copy{*this};
  ++*this;
  return copy;
}

template <size_t alignment>
AlignedPtr<alignment> AlignedPtr<alignment>::operator--(int) & noexcept
{
  AlignedPtr<alignment> copy{*this};
  --*this;
  return copy;
}

template <size_t alignment>
AlignedPtr<alignment> AlignedPtr<alignment>::operator+(std::ptrdiff_t offset) const noexcept
{
  return AlignedPtr<alignment>(std::next(ptr_, offset * static_cast<std::ptrdiff_t>(alignment)));
}

template <size_t alignment>
AlignedPtr<alignment>& AlignedPtr<alignment>::operator+=(std::ptrdiff_t offset) noexcept
{
  *this = *this + offset;
  return *this;
}

template <size_t alignment>
AlignedPtr<alignment> AlignedPtr<alignment>::operator-(std::ptrdiff_t offset) const noexcept
{
  return *this + -offset;
}

template <size_t alignment>
AlignedPtr<alignment>& AlignedPtr<alignment>::operator-=(std::ptrdiff_t offset) noexcept
{
  *this = *this - offset;
  return *this;
}

template <size_t alignment>
bool AlignedPtr<alignment>::operator==(AlignedPtr<alignment> rhs) const noexcept
{
  return ptr_ == rhs.ptr_;
}

template <size_t alignment>
bool AlignedPtr<alignment>::operator!=(AlignedPtr<alignment> rhs) const noexcept
{
  return !(*this == rhs);
}

template <size_t alignment>
AlignedPtr<alignment>::AlignedPtr(std::byte* ptr) noexcept
  : ptr_{ptr}
{
}

} // namespace clockwork::pinion
