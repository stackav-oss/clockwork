// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>

namespace jewels::memory
{

/// An aligned storage type used to hold a type T.
/// @note See paper P1413R3 linked below for more details on why this
/// is used over std::aligned_storage.
/// https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p1413r3.pdf
template <class T>
struct AlignedStorage
{
  /// Bytes to store a T.
  /// @note This is intentionally a c-style array and not a std::array
  /// to match the proposed implementation in the paper.
  // NOLINTNEXTLINE(modernize-avoid-c-arrays)
  alignas(T) std::byte bytes[sizeof(T)];
};

/// A policy that governs the lifetime of an object using the normal
/// construtor and destructor.
template <class T>
struct ObjectPolicy
{
  using storage_type = AlignedStorage<T>;
  using value_type = T;
  using reference = T&;
  using const_reference = const T&;
  using pointer = T*;
  using const_pointer = const T*;

  /// Construct an object of type T in the storage.
  /// @param storage Where to construct the T.
  /// @param args A pack of args to pass to the constructor of T.
  template <class... Args>
  static void construct(storage_type& storage, Args&&... args);

  /// Destruct the T being held in the storage.
  /// @note This is UB if storage is not holding a valid T.
  /// @param storage Where the T is stored.
  static void destruct(storage_type& storage);

  /// Marshall an aligned storage as a pointer.
  /// @param storage A storage holding a valid T.
  /// @return A pointer to the underlying object.
  [[nodiscard]] static pointer ptr(storage_type& storage);

  /// Marshall an aligned storage as a const pointer.
  /// @param storage A storage holding a valid T.
  /// @return A pointer to the underlying object..
  [[nodiscard]] static const_pointer ptr(const storage_type& storage);

  /// Marshall an aligned storage as a reference.
  /// @note This is UB if a T has not been constructed in the storage.
  /// @param storage A storage holding a valid T.
  /// @return A reference to the underlying object.
  [[nodiscard]] static reference get(storage_type& storage);

  /// Marshall an aligned storage as a const reference.
  /// @note This is UB if a T has not been constructed in the storage.
  /// @param storage A storage holding a valid T.
  /// @return A reference to the underlying object..
  [[nodiscard]] static const_reference get(const storage_type& storage);

  static void ptr(storage_type&& storage) = delete;

  static void ptr(const storage_type&& storage) = delete;

  static void get(storage_type&& storage) = delete;

  static void get(const storage_type&& storage) = delete;
};

} // namespace jewels::memory

#include "jewels/memory/aligned_storage.inl"
