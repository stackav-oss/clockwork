// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/memory/error.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/std/expected.hh"

#include <gsl/pointers> // IWYU pragma: export

#include <cstdint>
#include <memory>

namespace jewels::memory
{
/// A pointer which cannot be null.
/// Usually this doesn't need to be used directly - see the other types such as `ObjectPtr`.
/// @tparam Ptr A pointer type.
template <class Ptr>
using NonNullPtr = gsl::strict_not_null<Ptr>;

/// A raw pointer which cannot be null.
template <class T>
using ObjectPtr = NonNullPtr<T*>;

/// A shared pointer which cannot be null.
template <class T>
using NonNullSharedPtr = NonNullPtr<std::shared_ptr<T>>;

/// A unique pointer which cannot be null.
/// @tparam T Object type associated with the unique pointer.
/// @tparam enable_polymorphic_deletion True enables polymorphic deletion behavior.
/// @tparam Alloc Allocator type
template <class T, bool enable_polymorphic_deletion = false, class Alloc = std::allocator<void>>
using NonNullUniquePtr = NonNullPtr<unique_ptr<T, enable_polymorphic_deletion, Alloc>>;

/// A pmr unique pointer which cannot be null.
/// @tparam T Object type associated with the unique pointer.
/// @tparam enable_polymorphic_deletion True enables polymorphic deletion behavior.
template <class T, bool enable_polymorphic_deletion = false>
using NonNullPmrUniquePtr = NonNullPtr<pmr_unique_ptr<T, enable_polymorphic_deletion>>;

/// Make an `ObjectPtr` from a reference.
/// @tparam T The type of `object`.
/// @param[in] object An object.
/// @returns `ObjectPtr` to `object`.
template <class T>
[[nodiscard]] ObjectPtr<T> make_non_null_from_ref(T& object) noexcept;

/// Make an `ObjectPtr` from a const reference.
/// @tparam T The type of `object`.
/// @param[in] object An object.
/// @returns `ObjectPtr` to `object`.
template <class T>
[[nodiscard]] ObjectPtr<const T> make_non_null_from_ref(const T& object) noexcept;

/// Attempt to make a non-null pointer from a pointer.
/// @tparam Ptr A pointer type.
/// @param[in] ptr A pointer.
/// @returns Either an `NonNullPtr` or a `MemoryError` if `ptr` is `nullptr`.
template <class Ptr>
[[nodiscard]] jewels::expected<NonNullPtr<Ptr>, MemoryError> try_make_non_null(Ptr ptr) noexcept;

/// Make a non-null shared pointer via std::make_shared
/// @tparam T The type of the `object`.
/// @tparam Args Constructor arguments.
/// @param[in] args Constructor arguments.
/// @return Non-null shared pointer to allocated instance.
/// @throws Any exceptions thrown by the constructor.
/// @throws std::bad_alloc if the allocation fails.
template <typename T, typename... Args>
[[nodiscard]] NonNullSharedPtr<T> make_shared(Args&&... args);

/// Allocate a non-null shared pointer via std::allocate_shared
/// @tparam T The type of the `object`.
/// @tparam Alloc Allocator type
/// @tparam Args Constructor arguments.
/// @param[in] alloc Memory allocator.
/// @param[in] args Constructor arguments.
/// @return Non-null shared pointer to allocated instance.
/// @throws Any exceptions thrown by the constructor.
/// @throws std::bad_alloc if the allocation fails.
template <typename T, typename Alloc, typename... Args>
[[nodiscard]] NonNullSharedPtr<T> allocate_shared(const Alloc& alloc, Args&&... args);

/// Cast to a uintptr_t.
/// @param ptr The pointer to cast.
/// @return The address of the pointer as a uintptr_t.
template <class T>
[[nodiscard]] uintptr_t to_uintptr_t(const T* ptr) noexcept;

} // namespace jewels::memory

#include "jewels/memory/pointers.inl"
