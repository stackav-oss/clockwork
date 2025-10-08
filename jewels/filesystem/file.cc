// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/filesystem/file.hh"

#include "jewels/log_cerr/log_cerr.hh"

#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <memory_resource>
#include <optional>
#include <sys/stat.h>
#include <sys/syscall.h> // IWYU pragma: keep
#include <sys/types.h>
#include <unistd.h>
#include <utility>
// IWYU pragma: no_include <bits/syscall-64.h>

namespace jewels::filesystem
{
namespace
{
// Permissions with which files are created, though may be further limited by the process umask
// Note that this is always passed to `open` since the `mode` parameter is ignored without CREAT or TMPFILE and `open`
// can only create regular files, not directories so these are sane defaults.
constexpr mode_t default_file_mode = 0666;

// Permissions with which directories are created, though may be further limited by the process umask
constexpr mode_t default_directory_mode = 0777;
} // namespace

// File has no special flags
const int File::open_flags = 0;
// Directory includes O_DIRECTORY to ensure it doesn't open a file
// O_PATH would make sense for a default access mode, but the API isn't rich enough to support something like that
const int Directory::open_flags = O_DIRECTORY;
// Provides the AT_FDCWD constant for use by the various `at` type functions without needing to include / "know" about
// the implementation details
const int Directory::at_cwd = AT_FDCWD;

ErrorCode FileBase::report_io_err(int err, std::string_view message, std::string_view path)
{
  auto error = make_error_code(err);
  jewels::log_cerr_info("{} '{}': {}", message, path, error.message());
  return error;
}

jewels::expected<FileDescriptor, ErrorCode> FileBase::open(std::string_view path, auto&& opener)
{
  auto null_path = PathString::try_make(path);
  if (!null_path)
  {
    return jewels::unexpected(report_io_err(ENAMETOOLONG, "Failed to open", path));
  }
  FileDescriptor file_descriptor{opener(null_path->data())};
  if (!file_descriptor)
  {
    return jewels::unexpected(report_io_err(errno, "Failed to open", path));
  }
  return file_descriptor;
}

jewels::expected<FileDescriptor, ErrorCode> FileBase::open(std::string_view path, int flags)
{
  return open(
    path,
    [flags](const char* internal_path)
    {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) - Necessary for POSIX ::open which uses varargs for mode
      return ::open(internal_path, flags, default_file_mode);
    });
}

jewels::expected<FileDescriptor, ErrorCode> FileBase::openat(int dirfd, std::string_view path, int flags)
{
  return open(
    path,
    [dirfd, flags](const char* internal_path)
    {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) - Necessary for POSIX ::openat which uses varargs for mode
      return ::openat(dirfd, internal_path, flags, default_file_mode);
    });
}

// NOLINTNEXTLINE(readability-make-member-function-const) updates file descriptor's state
jewels::expected<off_t, ErrorCode> FileBase::seek_set(off_t position)
{
  off_t result = ::lseek(descriptor(), position, SEEK_SET);
  if (result == static_cast<off_t>(-1))
  {
    return jewels::unexpected(make_error_code(errno));
  }
  return result;
}

int FileBase::set_default_flags(int flags, int extra)
{
  // If unset, set to defaults
  // Note the the sentinel is -1 because 0 is a generally valid value for flags
  if (flags == -1)
  {
    flags = O_RDONLY;
  }
  auto uflags = static_cast<unsigned int>(flags);
  // Apply requested extra flags
  uflags |= static_cast<unsigned int>(extra);
  // This is a simplified API where it always make sense to use O_CLOEXEC
  uflags |= O_CLOEXEC;
  return static_cast<int>(uflags);
}

jewels::expected<size_t, ErrorCode> File::pread(std::span<std::byte> data, off_t pos)
{
  size_t total = 0;
  for (int retry = max_retries; retry > 0; retry--)
  {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic) pointer arithmetic required for pread api
    const ssize_t ret = ::pread(descriptor(), data.data() + total, data.size() - total, pos);
    if (ret > 0)
    {
      total += static_cast<size_t>(ret);
      pos += ret;
    }
    if (ret == 0 || total >= data.size())
    {
      break;
    }
    if (ret < 0 && errno != EINTR)
    {
      return jewels::unexpected(make_error_code(errno));
    }
  }
  return total;
}

jewels::expected<size_t, ErrorCode> File::pread_str(std::span<char> data, off_t pos)
{
  const auto ret = ::pread(descriptor(), data.data(), data.size(), pos);
  if (ret < 0)
  {
    return jewels::unexpected(make_error_code(errno));
  }
  if (!data.empty())
  {
    data[std::min(data.size() - 1, static_cast<size_t>(ret))] = 0;
  }
  return static_cast<size_t>(ret);
}

jewels::expected<std::pmr::vector<std::byte>, ErrorCode>
File::read_all(jewels::memory::MemoryResource memres, size_t max)
{
  struct stat statbuf = {};
  if (::fstat(descriptor(), &statbuf) == -1)
  {
    return jewels::unexpected(make_error_code(errno));
  }
  if (statbuf.st_size < 0) [[unlikely]]
  {
    return jewels::unexpected(make_error_code(ENOTSUP));
  }
  const auto size = static_cast<size_t>(statbuf.st_size);
  if (max > 0 && size > max)
  {
    return jewels::unexpected(make_error_code(EFBIG));
  }
  auto blob = std::pmr::vector<std::byte>(size + 1, std::byte{}, memres);
  auto read = pread(blob);
  if (!read)
  {
    return jewels::unexpected(make_error_code(errno));
  }
  if (*read != size)
  {
    return jewels::unexpected(make_error_code(EIO));
  }
  blob.resize(*read);
  return std::move(blob);
}

jewels::expected<filesystem::Directory, ErrorCode> Directory::create_open(int dirfd, std::string_view name)
{
  // POSIX doesn't offer an atomic "open, create if needed" for directories so this sort of racy pattern is required.
  // One could attempt to open the directory first, which might save a bit of time in the average case but that has its
  // own idiosyncrasies and this API is more targeted at creation.
  // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage) TODO(DX-1794): Fix this
  const int result = ::mkdirat(dirfd, name.data(), default_directory_mode);
  if (result == -1 && errno != EEXIST)
  {
    return jewels::unexpected(report_io_err(errno, "Failed to create", name));
  }
  return filesystem::Directory::open(dirfd, name);
}

jewels::expected<size_t, ErrorCode> Directory::read_entries(std::span<std::byte> buffer)
{
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) Required for syscall interface which uses varargs
  const auto ret = ::syscall(SYS_getdents64, descriptor(), buffer.data(), buffer.size());
  if (ret < 0)
  {
    return jewels::unexpected(make_error_code(errno));
  }
  return static_cast<size_t>(ret);
}

} // namespace jewels::filesystem
