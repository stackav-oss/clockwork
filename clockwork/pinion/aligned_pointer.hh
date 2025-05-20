// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/math/power_of_two.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"

#include <cstddef>

namespace clockwork::pinion
{

/// A strong type for an aligned non-null pointer.
/// @tparam alignment Alignment for the pointer.
template <size_t alignment>
class AlignedPtr
{
  static_assert(jewels::math::is_power_of_two(alignment), "Alignment must be a power of two.");

public:
  /// Try to construct an aligned pointer.
  /// @param ptr The pointer that should be aligned.
  /// @return An aligned pointer if the pointer is aligned or unexpected otherwise.
  [[nodiscard]] static jewels::expected<AlignedPtr, jewels::MonoError>
  try_make(jewels::memory::ObjectPtr<std::byte> ptr) noexcept;

  /// Construct from a reference to an object that will be treated as bytes.
  /// @param object The object that is aligned.
  /// @return An aligned pointer.
  template <class Object>
  [[nodiscard]] static AlignedPtr from_ref(Object& object) noexcept;

  AlignedPtr(const AlignedPtr&) noexcept = default;
  AlignedPtr(AlignedPtr&&) noexcept = default;
  AlignedPtr& operator=(const AlignedPtr&) noexcept = default;
  AlignedPtr& operator=(AlignedPtr&&) noexcept = default;

  ~AlignedPtr() noexcept = default;

  /// Get the underlying pointer.
  /// @{
  [[nodiscard]] std::byte* get() const noexcept;
  [[nodiscard]] std::byte* operator->() const noexcept;
  /// @}

  /// Increment overloads.
  /// @{
  AlignedPtr& operator++() noexcept;
  AlignedPtr operator++(int) & noexcept;
  /// @}

  /// Decrement overloads.
  /// @{
  AlignedPtr& operator--() noexcept;
  AlignedPtr operator--(int) & noexcept;
  /// @}

  /// Addition overloads.
  /// @{
  [[nodiscard]] AlignedPtr operator+(std::ptrdiff_t offset) const noexcept;
  AlignedPtr& operator+=(std::ptrdiff_t offset) noexcept;
  /// @}

  /// Subtraction overloads.
  /// @{
  [[nodiscard]] AlignedPtr operator-(std::ptrdiff_t offset) const noexcept;
  AlignedPtr& operator-=(std::ptrdiff_t offset) noexcept;

  /// Equality overloads.
  /// @{
  [[nodiscard]] bool operator==(AlignedPtr rhs) const noexcept;
  [[nodiscard]] bool operator!=(AlignedPtr rhs) const noexcept;
  /// @}

private:
  /// Construct an aligned pointer.
  /// @param ptr The aligned pointer.
  explicit AlignedPtr(std::byte* ptr) noexcept;

  /// The underling pointer.
  std::byte* ptr_;
};

} // namespace clockwork::pinion

#include "clockwork/pinion/aligned_pointer.inl"
