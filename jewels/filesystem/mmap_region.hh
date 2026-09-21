// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/std/expected.hh"

#include <cstddef>
#include <span>

namespace jewels::filesystem
{

///
/// Simple RAII manager for a monolithic mmaped region.
///
class MMapRegion
{
public:
  /// Creates a file backed mmap at a non-fixed location with offset 0
  /// @param file_descriptor The file descriptor to map
  /// @param length The length of the region, expected to be equal to the file size
  /// @param prot Protection (see man mmap)
  /// @param flags Flags (see man mmap)
  /// @return The mmap handle or errno
  static jewels::expected<MMapRegion, int> create(int file_descriptor, size_t length, int prot, int flags);

  ~MMapRegion();
  MMapRegion(const MMapRegion&) = delete;
  MMapRegion& operator=(const MMapRegion&) = delete;
  MMapRegion(MMapRegion&& /*other*/) noexcept;
  MMapRegion& operator=(MMapRegion&& /*other*/) noexcept;
  friend void swap(MMapRegion& lhs, MMapRegion& rhs) noexcept;

  /// Checks if the mapping is valid (guard_ptr!=MAP_FAILED)
  /// `create` will return an error code instead of an invalid map, but this may still be false in some edge cases, e.g.
  /// if this was moved from.
  explicit operator bool() const noexcept;

  /// Get a byte span representing the mapped region
  [[nodiscard]] std::span<std::byte> to_span() const noexcept
  {
    return {static_cast<std::byte*>(ptr_), size_};
  }

private:
  MMapRegion(void* guard_ptr, size_t guard_size, void* ptr, size_t size) noexcept;

  void* guard_ptr_;
  size_t guard_size_;
  void* ptr_;
  size_t size_;
};

} // namespace jewels::filesystem
