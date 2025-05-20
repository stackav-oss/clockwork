// IWYU pragma: private, include "jewels/filesystem/file.hh"
#pragma once
#include "jewels/filesystem/file.hh"

#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/std/expected.hh"

#include <array>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <string_view>
#include <sys/types.h>
#include <utility>

namespace jewels::filesystem
{

inline FileBase::FileBase(jewels::expected<FileDescriptor, ErrorCode> expected_file_descriptor)
{
  if (!expected_file_descriptor)
  {
    throw std::runtime_error(expected_file_descriptor.error().message());
  }
  fd_ = std::move(expected_file_descriptor).value();
}

inline FileBase::FileBase(FileDescriptor file_descriptor)
  : fd_(std::move(file_descriptor))
{
}

inline int FileBase::descriptor() const
{
  return *fd_;
}

template <typename T>
inline detail::FileCommon<T>::FileCommon(std::string_view path, int flags)
  : FileBase(FileBase::open(path, set_default_flags(flags, T::open_flags)))
{
}

template <typename T>
inline detail::FileCommon<T>::FileCommon(int dirfd, std::string_view path, int flags)
  : FileBase(FileBase::openat(dirfd, path, set_default_flags(flags, T::open_flags)))
{
}

template <typename T>
jewels::expected<T, ErrorCode> detail::FileCommon<T>::open(std::string_view path, int flags)
{
  auto expected_fd = FileBase::open(path, set_default_flags(flags, T::open_flags));
  if (!expected_fd)
  {
    return jewels::unexpected(expected_fd.error());
  }
  return T(std::move(expected_fd).value());
}

template <typename T>
jewels::expected<T, ErrorCode> detail::FileCommon<T>::open(int dirfd, std::string_view path, int flags)
{
  auto expected_fd = FileBase::openat(dirfd, path, set_default_flags(flags, T::open_flags));
  if (!expected_fd)
  {
    return jewels::unexpected(expected_fd.error());
  }
  return T(std::move(expected_fd).value());
}

template <typename... Args>
jewels::expected<filesystem::Directory, ErrorCode>
Directory::create_open(int dirfd, std::string_view name1, std::string_view name2, Args... names)
{
  auto next = create_open(dirfd, name1);
  if (!next)
  {
    return next;
  }
  // NOLINTNEXTLINE(readability-suspicious-call-argument) Argument name shadowing is due to the recursive call.
  return create_open(next->descriptor(), name2, names...);
}

jewels::expected<void, ErrorCode> Directory::process(auto&& processor)
{
  struct linux_dirent64 // NOLINT(readability-identifier-naming) using the documented naming
  {
    ino64_t d_ino;           /* 64-bit inode number */
    off64_t d_off;           /* 64-bit offset to next structure */
    unsigned short d_reclen; /* Size of this dirent NOLINT(google-runtime-int) */
    unsigned char d_type;    /* File type */
    char d_name[1];          /* Filename (null-terminated) NOLINT(modernize-avoid-c-arrays) */
  };
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init) Buffer is immediately populated by read_entries.
  std::array<std::byte, buffer_size> entry_buffer;
  const linux_dirent64* entry = nullptr;
  if (auto ret = seek_set(0); !ret)
  {
    return jewels::unexpected(ret.error());
  }
  while (true)
  {
    const auto expected_bytes_read = read_entries(entry_buffer);
    if (!expected_bytes_read)
    {
      return jewels::unexpected(expected_bytes_read.error());
    }
    if (expected_bytes_read.value() == 0)
    {
      break;
    }
    for (size_t offset = 0; offset < expected_bytes_read.value(); offset += entry->d_reclen)
    {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) Required to interpret data as directory entry
      entry = reinterpret_cast<decltype(entry)>(entry_buffer.data() + offset);
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay) Intentional decay
      auto result = processor(descriptor(), std::string_view(entry->d_name), entry->d_type);
      if (!result)
      {
        return jewels::unexpected(result.error());
      }
      if (!result.value())
      {
        return {};
      }
    }
  }
  return {};
}
} // namespace jewels::filesystem
