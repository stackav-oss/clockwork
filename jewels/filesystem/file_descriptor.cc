// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/filesystem/file_descriptor.hh"

#include "jewels/filesystem/error_code.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/std/expected.hh"

#include <cerrno>
#include <ostream>
#include <tuple>
#include <unistd.h>

namespace jewels::filesystem
{

FileDescriptor::FileDescriptor(int32_t file_desc) noexcept
  : file_desc_(file_desc)
{
}

FileDescriptor::~FileDescriptor() noexcept
{
  forced_close();
}

FileDescriptor::FileDescriptor(FileDescriptor&& other) noexcept
  : file_desc_(other.file_desc_)
{
  other.file_desc_ = -1;
}

FileDescriptor& FileDescriptor::operator=(FileDescriptor&& other) noexcept
{
  if (this != &other)
  {
    forced_close();
    file_desc_ = other.file_desc_;
    other.file_desc_ = -1;
  }
  return *this;
}

[[nodiscard]] FileDescriptor::operator bool() const noexcept
{
  return file_desc_ >= 0;
}

[[nodiscard]] int32_t FileDescriptor::operator*() const noexcept
{
  return file_desc_;
}

[[nodiscard]] int32_t FileDescriptor::release() noexcept
{
  const auto ret_fd = file_desc_;
  file_desc_ = -1;
  return ret_fd;
}

[[nodiscard]] jewels::expected<void, ErrorCode> FileDescriptor::close() noexcept
{
  if (file_desc_ < 0)
  {
    return {};
  }
  const auto close_rc = ::close(file_desc_);
  if (close_rc < 0)
  {
    const auto error = make_error_code(errno);
    jewels::log_cerr_error("Failed to close file descriptor {}: {}", file_desc_, error.message());
    file_desc_ = -1;
    return jewels::unexpected(error);
  }
  file_desc_ = -1;
  return {};
}

void FileDescriptor::forced_close() noexcept
{
  std::ignore = close();
}

std::ostream& operator<<(std::ostream& ostream, const FileDescriptor& file_desc)
{
  ostream << "{" << *file_desc << "}";
  return ostream;
}

} // namespace jewels::filesystem
