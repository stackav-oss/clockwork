// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/filesystem/mmap_region.hh"

#include "jewels/filesystem/error_code.hh"
#include "jewels/log_cerr/log_cerr.hh"

#include <cerrno>
#include <cstdint>
#include <span>
#include <sys/mman.h>
#include <unistd.h>
#include <utility>

namespace jewels::filesystem
{

jewels::expected<MMapRegion, int> MMapRegion::create(int file_descriptor, size_t length, int prot, int flags)
{
  const auto page_size = static_cast<size_t>(sysconf(_SC_PAGESIZE));
  const auto guard_length = length + (page_size * 2U);
  void* guard_ptr = ::mmap(nullptr, guard_length, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (guard_ptr == MAP_FAILED)
  {
    return jewels::unexpected(errno);
  }
  const auto guard_span = std::span{static_cast<char*>(guard_ptr), guard_length};
  const auto map_flags = static_cast<int>(static_cast<uint32_t>(flags) | MAP_FIXED);
  void* ptr = ::mmap(&guard_span[page_size], length, prot, map_flags, file_descriptor, 0);
  if (ptr == MAP_FAILED)
  {
    return jewels::unexpected(errno);
  }
  return MMapRegion(guard_ptr, guard_length, ptr, length);
}

MMapRegion::MMapRegion(void* guard_ptr, size_t guard_size, void* ptr, size_t size) noexcept
  : guard_ptr_(guard_ptr), guard_size_(guard_size), ptr_(ptr), size_(size)
{
}

MMapRegion::MMapRegion(MMapRegion&& other) noexcept
  : guard_ptr_(other.guard_ptr_), guard_size_(other.guard_size_), ptr_(other.ptr_), size_(other.size_)
{
  other.guard_ptr_ = MAP_FAILED;
  other.ptr_ = MAP_FAILED;
}

MMapRegion& MMapRegion::operator=(MMapRegion&& other) noexcept
{
  MMapRegion tmp(std::move(other));
  swap(*this, tmp);
  return *this;
}

void swap(MMapRegion& lhs, MMapRegion& rhs) noexcept
{
  std::swap(lhs.guard_ptr_, rhs.guard_ptr_);
  std::swap(lhs.guard_size_, rhs.guard_size_);
  std::swap(lhs.ptr_, rhs.ptr_);
  std::swap(lhs.size_, rhs.size_);
}

MMapRegion::operator bool() const noexcept
{
  return guard_ptr_ != MAP_FAILED;
}

MMapRegion::~MMapRegion()
{
  if (guard_ptr_ != MAP_FAILED)
  {
    const int result = ::munmap(guard_ptr_, guard_size_);
    if (result == -1)
    {
      jewels::log_cerr_error("munamp failed with {}", jewels::filesystem::ErrorCode(errno));
    }
  }
}

} // namespace jewels::filesystem
