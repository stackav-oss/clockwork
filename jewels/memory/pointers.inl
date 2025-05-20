// IWYU pragma: private, include "jewels/memory/pointers.hh"

#pragma once

#include "jewels/memory/pointers.hh"

#include "jewels/memory/error.hh"
#include "jewels/std/expected.hh"

#include <cstdint>
#include <memory>
#include <utility>

namespace jewels::memory
{
template <class T>
[[nodiscard]] ObjectPtr<T> make_non_null_from_ref(T& object) noexcept
{
  return ObjectPtr<T>{&object};
}

template <class T>
[[nodiscard]] ObjectPtr<const T> make_non_null_from_ref(const T& object) noexcept
{
  return ObjectPtr<const T>{&object};
}

template <class Ptr>
[[nodiscard]] jewels::expected<NonNullPtr<Ptr>, MemoryError> try_make_non_null(Ptr ptr) noexcept
{
  return ptr != nullptr ? jewels::expected<NonNullPtr<Ptr>, MemoryError>{std::move(ptr)}
                        : jewels::unexpected(MemoryError::null_pointer_error);
}

template <typename T, typename... Args>
[[nodiscard]] NonNullUniquePtr<T> make_unique(Args&&... args)
{
  return NonNullUniquePtr<T>(std::make_unique<T>(std::forward<Args>(args)...));
}

template <typename T, typename... Args>
[[nodiscard]] NonNullSharedPtr<T> make_shared(Args&&... args)
{
  return NonNullSharedPtr<T>(std::make_shared<T>(std::forward<Args>(args)...));
}

template <typename T, typename Alloc, typename... Args>
[[nodiscard]] NonNullSharedPtr<T> allocate_shared(const Alloc& alloc, Args&&... args)
{
  return NonNullSharedPtr<T>(std::allocate_shared<T>(alloc, std::forward<Args>(args)...));
}

template <class T>
[[nodiscard]] uintptr_t to_uintptr_t(const T* ptr) noexcept
{
  // This is how to convert a pointer to a uintptr_t in c++.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  return reinterpret_cast<uintptr_t>(ptr);
}

} // namespace jewels::memory
