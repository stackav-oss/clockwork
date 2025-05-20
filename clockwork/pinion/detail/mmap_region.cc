// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/detail/mmap_region.hh"

#include "jewels/filesystem/error_code.hh"
#include "jewels/log_cerr/log_cerr.hh"

#include <cerrno>
#include <sys/mman.h>
#include <utility>

namespace clockwork::pinion
{

jewels::expected<MMapRegion, int> MMapRegion::create(int file_descriptor, size_t length, int prot, int flags)
{
  void* ptr = ::mmap(nullptr, length, prot, flags, file_descriptor, 0);
  if (ptr == MAP_FAILED)
  {
    return jewels::unexpected(errno);
  }
  return MMapRegion(ptr, length);
}

MMapRegion::MMapRegion(void* ptr, size_t size) noexcept
  : ptr_(ptr), size_(size)
{
}

MMapRegion::MMapRegion(MMapRegion&& other) noexcept
  : ptr_(other.ptr_), size_(other.size_)
{
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
  std::swap(lhs.ptr_, rhs.ptr_);
  std::swap(lhs.size_, rhs.size_);
}

MMapRegion::operator bool() const noexcept
{
  return ptr_ != MAP_FAILED;
}

MMapRegion::~MMapRegion()
{
  if (ptr_ != MAP_FAILED)
  {
    const int result = ::munmap(ptr_, size_);
    if (result == -1)
    {
      jewels::log_cerr_error("munamp failed with {}", jewels::filesystem::ErrorCode(errno));
    }
  }
}

} // namespace clockwork::pinion
