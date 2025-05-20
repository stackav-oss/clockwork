// IWYU pragma: private, include "jewels/memory/aligned_storage.hh"

#pragma once

#include "jewels/memory/aligned_storage.hh"

#include <new>
#include <type_traits>
#include <utility>

namespace jewels::memory
{

template <class T>
template <class... Args>
void ObjectPolicy<T>::construct(AlignedStorage<T>& storage, Args&&... args)
{
  new (static_cast<void*>(storage.bytes)) T{std::forward<Args>(args)...};
}

template <class T>
void ObjectPolicy<T>::destruct(AlignedStorage<T>& storage)
{
  if constexpr (!std::is_trivially_destructible_v<T>)
  {
    get(storage).~T();
  }
}

template <class T>
auto ObjectPolicy<T>::ptr(storage_type& storage) -> pointer
{
  // This cast is valid when a T lives in the byte buffer.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  return std::launder(reinterpret_cast<T*>(storage.bytes));
}

template <class T>
auto ObjectPolicy<T>::ptr(const storage_type& storage) -> const_pointer
{
  // This cast is valid when a T lives in the byte buffer.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  return std::launder(reinterpret_cast<const T*>(storage.bytes));
}

template <class T>
auto ObjectPolicy<T>::get(storage_type& storage) -> reference
{
  return *ptr(storage);
}

template <class T>
auto ObjectPolicy<T>::get(const storage_type& storage) -> const_reference
{
  return *ptr(storage);
}

} // namespace jewels::memory
