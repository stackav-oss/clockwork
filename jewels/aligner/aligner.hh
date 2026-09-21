// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <type_traits> // IWYU pragma: keep

namespace jewels
{

/// Buffer alignment helper class
/// @tparam alignment Buffer alignment
template <size_t alignment>
class Aligner
{
public:
  static_assert(alignment != 0U, "Alignment must be non-zero");
  static_assert((alignment & (alignment - 1U)) == 0U, "alignment must be a power of 2");

  /// Mask to 'and' with an offset to get alignment offset
  static constexpr size_t alignment_mask = ~(alignment - 1U);

  /// Test whether an offset is aligned
  /// @tparam OffsetType Offset value type
  /// @param[in] offset Offset value
  /// @return True if the offset is aligned
  template <typename OffsetType>
  [[nodiscard]] constexpr static bool is_aligned(OffsetType offset)
    requires(std::is_integral_v<OffsetType>);

  /// Get the next lowest aligned boundary for an offset if it is not already aligned
  /// @tparam OffsetType Offset value type
  /// @param[in] offset Offset value
  /// @return Offset aligned to the next lowest aligned boundary
  template <typename OffsetType>
  [[nodiscard]] constexpr static OffsetType align_prev(OffsetType offset)
    requires(std::is_integral_v<OffsetType>);

  /// Get the next highest aligned boundary for an offset if it is not already aligned
  /// @tparam OffsetType Offset value type
  /// @param[in] offset Offset value
  /// @return Offset aligned to the next highest aligned boundary
  template <typename OffsetType>
  [[nodiscard]] constexpr static OffsetType align_next(OffsetType offset)
    requires(std::is_integral_v<OffsetType>);

  /// Get the offset from the start of next lowest aligned boundary
  /// @tparam OffsetType Offset value type
  /// @param[in] offset Offset value
  /// @return Offset from the next lowest boundary or zero if the offset is already aligned
  template <typename OffsetType>
  [[nodiscard]] constexpr static OffsetType aligned_offset(OffsetType offset)
    requires(std::is_integral_v<OffsetType>);

  /// Get the remaining bytes before the start of th next highest aligned boundary
  /// @tparam OffsetType Offset value type
  /// @param[in] offset Offset value
  /// @return Number of bytes remaining before the start of the next boundary or zero if the offset is already aligned
  template <typename OffsetType>
  [[nodiscard]] constexpr static OffsetType aligned_remainder(OffsetType offset)
    requires(std::is_integral_v<OffsetType>);

  /// Test whether a pointer is aligned
  /// @tparam T Pointer value type
  /// @param[in] ptr Pointer value
  /// @return True if the pointer is aligned
  template <typename T>
  [[nodiscard]] static bool ptr_is_aligned(const T* ptr)
    requires(sizeof(T) == 1U);

  /// Get the offset from the start of next lowest aligned boundary of a pointer
  /// @tparam T Pointer value type
  /// @param[in] ptr Pointer value
  /// @return Offset from the next lowest boundary or zero if the pointer is already aligned
  template <typename T>
  [[nodiscard]] static ptrdiff_t ptr_aligned_offset(const T* ptr)
    requires(sizeof(T) == 1U);

  /// Get the remaining bytes before the start of th next highest aligned boundary of a pointer
  /// @tparam T Pointer value type
  /// @return Number of bytes remaining before the start if the next boundary or zero if the offset is already aligned
  template <typename T>
  [[nodiscard]] static ptrdiff_t ptr_aligned_remainder(const T* ptr)
    requires(sizeof(T) == 1U);

private:
  /// Test whether an offset is aligned
  /// @param[in] offset Offset value
  /// @return True if the offset is aligned
  [[nodiscard]] constexpr static bool is_aligned_impl(size_t offset);

  /// Get the next lowest aligned boundary for an offset if it is not already aligned
  /// @param[in] offset Offset value
  /// @return Offset aligned to the next lowest aligned boundary
  [[nodiscard]] constexpr static size_t align_prev_impl(size_t offset);

  /// Get the next highest aligned boundary for an offset if it is not already aligned
  /// @param[in] offset Offset value
  /// @return Offset aligned to the next highest aligned boundary
  [[nodiscard]] constexpr static size_t align_next_impl(size_t offset);

  /// Get the offset from the start of next lowest aligned boundary
  /// @param[in] offset Offset value
  /// @return Offset from the next lowest boundary or zero if the offset is already aligned
  [[nodiscard]] constexpr static size_t aligned_offset_impl(size_t offset);

  /// Get the remaining bytes before the start of the next highest aligned boundary
  /// @param[in] offset Offset value
  /// @return Number of bytes remaining before the start of the next boundary or zero if the offset is already aligned
  [[nodiscard]] constexpr static size_t aligned_remainder_impl(size_t offset);
};

} // namespace jewels

#include "jewels/aligner/aligner.inl"
