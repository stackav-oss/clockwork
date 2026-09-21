// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/memory/allocator_deleter.hh"

#include <memory>
#include <memory_resource>

namespace jewels::memory
{

/// A unique pointer.
/// @tparam T Object type associated with the unique pointer.
/// @tparam enable_polymorphic_deletion True enables polymorphic deletion behavior.
/// @tparam Alloc Allocator type
template <typename T, bool enable_polymorphic_deletion = false, typename Alloc = std::allocator<void>>
using unique_ptr = std::unique_ptr<T, AllocatorDeleter<T, enable_polymorphic_deletion, Alloc>>;

/// A pmr unique pointer.
/// @tparam T Object type associated with the unique pointer.
/// @tparam enable_polymorphic_deletion True enables polymorphic deletion behavior.
template <typename T, bool enable_polymorphic_deletion = false>
using pmr_unique_ptr = unique_ptr<T, enable_polymorphic_deletion, std::pmr::polymorphic_allocator<void>>;

/// Make a non-null unique pointer via allocate_unique using std::allocator<T>.
/// @tparam T The type of the `object`.
/// @tparam enable_polymorphic_deletion True enables polymorphic deletion behavior.
/// @tparam Args Constructor arguments deduced from `args`.
/// @param[in] args Constructor arguments.
/// @return Non-null unique pointer to allocated instance.
/// @throws Any exceptions thrown by the allocator or constructor.
template <typename T, bool enable_polymorphic_deletion = false, typename... Args>
[[nodiscard]] unique_ptr<T, enable_polymorphic_deletion> make_unique(Args&&... args);

/// Allocate a non-null unique pointer.
/// @tparam T The type of the `object`.
/// @tparam enable_polymorphic_deletion True enables polymorphic deletion behavior.
/// @tparam Alloc Allocator type deduced from `alloc`.
/// @tparam Args Constructor arguments deduced from `args`.
/// @param[in] alloc Memory allocator.
/// @param[in] args Constructor arguments.
/// @return Non-null unique pointer to allocated instance.
/// @throws Any exceptions thrown by the allocator or constructor.
template <typename T, bool enable_polymorphic_deletion = false, typename Alloc, typename... Args>
[[nodiscard]] unique_ptr<T, enable_polymorphic_deletion, Alloc> allocate_unique(const Alloc& alloc, Args&&... args);

/// Allocate a non-null pmr unique pointer.
/// @tparam T The type of the `object`.
/// @tparam enable_polymorphic_deletion True enables polymorphic deletion behavior.
/// @tparam Args Constructor arguments deduced from `args`.
/// @param[in] alloc Memory allocator.
/// @param[in] args Constructor arguments.
/// @return Non-null unique pointer to allocated instance.
/// @throws Any exceptions thrown by the allocator or constructor.
template <typename T, bool enable_polymorphic_deletion = false, typename... Args>
[[nodiscard]] pmr_unique_ptr<T, enable_polymorphic_deletion>
make_pmr_unique(const std::pmr::polymorphic_allocator<T>& alloc, Args&&... args);

/// Convenience wrapper around make_pmr_unique with polymorphic deletion support enabled, which allows for safe
/// polymorphic use cases where derived types may be deleted through base pointers.
/// @tparam T The type of the `object`.
/// @tparam Args Constructor arguments deduced from `args`.
/// @param[in] alloc Memory allocator.
/// @param[in] args Constructor arguments.
/// @return Non-null unique pointer to allocated instance.
/// @throws Any exceptions thrown by the allocator or constructor.
template <typename T, typename... Args>
[[nodiscard]] pmr_unique_ptr<T, true>
make_polymorphic_pmr_unique(const std::pmr::polymorphic_allocator<T>& alloc, Args&&... args);

/// Converts a unique pointer from monomorphic to polymorphic deletion semantics.
///
/// This helper provides the intentional conversion path when the deleter conversion is
/// explicit, avoiding accidental implicit conversion in generic `std::unique_ptr` code.
/// @tparam T Object type managed by the pointer deduced from `ptr`.
/// @tparam Alloc Allocator type associated with the deleter deduced from `ptr`.
/// @param[in] ptr Source pointer with monomorphic deleter.
/// @return Pointer to the same object, now using a polymorphic deleter.
template <typename T, typename Alloc>
[[nodiscard]] std::unique_ptr<T, PolymorphicDeleter<Alloc>>
to_polymorphic(std::unique_ptr<T, MonomorphicDeleter<Alloc>>&& mono_ptr);

} // namespace jewels::memory

#include "jewels/memory/pmr_unique_ptr.inl"
