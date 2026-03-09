// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/memory/memory_resource.hh"

#include <cstddef>
#include <memory>
#include <memory_resource>
#include <optional>
#include <type_traits>

namespace jewels::memory
{
namespace detail
{
template <typename T, bool supports_polymorphic_deletion>
struct PolymorphicDeleterFields
{
};
template <typename T>
struct PolymorphicDeleterFields<T, true>
{
  size_t allocation_size_{sizeof(T)};
  size_t alignment_{alignof(T)};
};

template <typename T, bool supports_polymorphic_deletion = false>
class PmrDeleter final : private PolymorphicDeleterFields<T, supports_polymorphic_deletion>
{
  using AllocType = std::pmr::polymorphic_allocator<T>;
  using AllocTraits = std::allocator_traits<AllocType>;

public:
  PmrDeleter() = default;
  explicit PmrDeleter(jewels::memory::MemoryResource memres);

  /// Allow converting deleters for different, but compatible types, e.g., for polymorphic unique_ptr use
  template <typename U>
  // NOLINTNEXTLINE(google-explicit-constructor)
  PmrDeleter(const PmrDeleter<U, true>& other)
    requires supports_polymorphic_deletion && std::is_base_of_v<T, U> && std::has_virtual_destructor_v<T>;

  void operator()(typename AllocTraits::pointer object);

  template <typename U, bool inner_supports_polymorphic_deletion>
  friend class PmrDeleter;

private:
  std::optional<jewels::memory::MemoryResource> memres_;
};
} // namespace detail

///
/// Alias for a unique_ptr using a pmr-aware deleter.
/// Construct using `make_pmr_unique()`
/// @tparam supports_polymorphic_deletion If true, the deleter can be converted to compatible types to support
/// polymorphic deletion.
///
template <typename T, bool supports_polymorphic_deletion = false>
using pmr_unique_ptr = std::unique_ptr<T, detail::PmrDeleter<T, supports_polymorphic_deletion>>;

///
/// Creates an object much like `std::make_unique` but uses the provided memory resource for the heap storage.
/// @param memres The memory resource that should be used for allocation and (later) deallocation
/// @param args A pack of args used to construct the new element.
/// @tparam supports_polymorphic_deletion If true, the deleter can be converted to compatible types to support
/// polymorphic deletion.
///
template <typename T, bool supports_polymorphic_deletion = false, typename... Args>
auto make_pmr_unique(jewels::memory::MemoryResource memres, Args&&... args);

///
/// Convenience wrapper around make_pmr_unique with polymorphic deletion support enabled, which allows for safe
/// polymorphic use cases where derived types may be deleted through base pointers.
///
template <typename T, typename... Args>
auto make_polymorphic_pmr_unique(jewels::memory::MemoryResource memres, Args&&... args);

} // namespace jewels::memory

#include "jewels/memory/pmr_unique_ptr.inl"
