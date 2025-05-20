// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

/// @file Lightweight file / directory access tools.  Originally intended for sysfs monitoring where heavier weight
/// paths and allocation-based tooling wasn't needed.

#pragma once

#include "jewels/container/bounded_string.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"

#include <climits>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <sys/types.h>
#include <vector>

namespace jewels::filesystem
{

/// Simple type for opening a path and closing on destruction.
/// Unlike a basic FileDescriptor, guarantees the descriptor is a valid (please don't `::close(inst.descriptor())`).
/// Contains some common operations that are common to more than one 'type' of file descriptor.
class FileBase
{
public:
  /// The maximum path name supported by this API, excluding the null terminator.  Note that there is a PATH_MAX macro
  /// in limits.h but it isn't really enforced and is largely meaningless so defining it here allows us to tune this as
  /// needed. This matters because the interfaces take string_view while the POSIX APIs take a null terminated string.
  /// string_view is defined by a size and not null termination, so even if most string_views probably have a null
  /// immediately after the size, checking that is undefined behavior in general so the only safe option is to copy in
  /// the input string to a null terminated buffer.  This gives that size, which represents a limit that should be
  /// similar to what one would expect from the underlying system.
  static constexpr size_t path_max = 4095;

  /// This provides a temporary buffer for null terminating strings to be used for paths
  using PathString = container::BoundedString<path_max>;

  /// This provides a temporary buffer for null terminating strings to be used for file names
  /// Unlike paths, the NAME_MAX limit is largely respected, though technically it can depend on filesystem (e.g. exFAT
  /// is 255 UTF-16 characters, which could use more than 255 bytes in UTF-8).
  using FileNameString = container::BoundedString<NAME_MAX>;

  ~FileBase() = default;
  FileBase(const FileBase&) = delete;
  FileBase(FileBase&&) = default;
  FileBase& operator=(const FileBase&) = delete;
  FileBase& operator=(FileBase&&) = default;

  /// Gets the descriptor for the open file
  [[nodiscard]] int descriptor() const;

  /// Wraps `lseek(descriptor(), position, SEEK_SET)`
  /// This is marked const since the linter doesn't realize that the internal
  jewels::expected<off_t, ErrorCode> seek_set(off_t position);

protected:
  // Size to use for buffers to make the linter quiet.  Use buffer.size() instead of this.
  static constexpr size_t buffer_size = 8192;

  /// Convert `err` (==errno) to ErrorCode and log_cerr_error "message 'path': error"
  static ErrorCode report_io_err(int err, std::string_view message, std::string_view path);

  /// Creates the common base from the descriptor, if set, otherwise throws the error
  explicit FileBase(jewels::expected<FileDescriptor, ErrorCode> expected_file_descriptor);

  /// Creates the common base from a file descriptor without validation
  explicit FileBase(FileDescriptor file_descriptor);

  /// Generalized opening function to handle errors, takes a lambda to do the actual opening
  static jewels::expected<FileDescriptor, ErrorCode> open(std::string_view path, auto&& opener);

  /// Try to open the file with the given path
  static jewels::expected<FileDescriptor, ErrorCode> open(std::string_view path, int flags);

  /// Try to open the file with the given path
  static jewels::expected<FileDescriptor, ErrorCode> openat(int dirfd, std::string_view path, int flags);

  /// Sets the default flags if none were provided and applies any additional flags
  /// @param flags a valid combination of flags for ::open or -1 if "unset"
  /// @param extra additional flags that should always be included (e.g. O_DIRECTORY for Directory)
  static int set_default_flags(int flags, int extra);

private:
  FileDescriptor fd_;
};

namespace detail
{

/// A CRTP mixin that offers a public static constructor functions with the correct return type
template <typename T>
class FileCommon : public FileBase
{
public:
  /// Open the file with the given path, throwing on error
  // We have to keep this public because `using` a constructor doesn't allow you to change its visibility to public.
  // NOLINTNEXTLINE(bugprone-crtp-constructor-accessibility)
  explicit FileCommon(std::string_view path, int flags = -1);

  /// Open the file with the given path relative to the opened directory descriptor `dirfd`
  // We have to keep this public because `using` a constructor doesn't allow you to change its visibility to public.
  // NOLINTNEXTLINE(bugprone-crtp-constructor-accessibility)
  explicit FileCommon(int dirfd, std::string_view path, int flags = -1);

  /// Open the file with the given path, returning a valid FileBase subclass or an error_code
  static jewels::expected<T, ErrorCode> open(std::string_view path, int flags = -1);

  /// Open the file with the given path, relative to `dirfd`, returning a valid FileBase subclass or an error_code
  static jewels::expected<T, ErrorCode> open(int dirfd, std::string_view path, int flags = -1);

protected:
  using FileBase::FileBase;
};

} // namespace detail

/// subclass of FileBase, strong typed for a normal file
class File : public detail::FileCommon<File>
{
public:
  /// Maximum number of times to attempt to re-read a file to account for interruptions
  static constexpr uint32_t max_retries = 5;

  using detail::FileCommon<File>::FileCommon;

  /// Reads bytes from the file into the buffer from the given position.  The file's internal cursor is not updated, so
  /// consecutive calls will return the same data (absent of outsize influences)
  /// @param data the buffer to hold the data
  /// @param pos the position to start reading from, in bytes from the start of the file
  [[nodiscard]] jewels::expected<size_t, ErrorCode> pread(std::span<std::byte> data, off_t pos = 0);

  /// Same as `pread` but returns the read value as a string.  It will read at most one byte less than `data.size()` and
  /// ensures that the string is null terminated.  Note that this doesn't guarantee that the string_view is otherwise
  /// free of null bytes.
  /// @param data the buffer to hold the data.  Reads at most `data.size() - 1` bytes
  /// @param pos the position to start reading from, in bytes from the start of the file
  [[nodiscard]] jewels::expected<size_t, ErrorCode> pread_str(std::span<char> data, off_t pos = 0);

  /// Reads in the full file.  Uses `::stat` to determine the size, allocates a buffer using `memres` and then uses
  /// `pread` to fill the buffer, validating that the correct number of bytes were read WARNING: May misbehave if this
  /// isn't a regular file
  /// @param memres allocator to use to provide the memory
  /// @param max if >0 then the maximum allowed size to read, EFBIG is returned if the file is larger than this
  [[nodiscard]] jewels::expected<std::pmr::vector<std::byte>, ErrorCode>
  read_all(jewels::memory::MemoryResource memres, size_t max = 0);

private:
  friend detail::FileCommon<File>;

  /// Extra flags to set when opening
  static const int open_flags;
};

/// subclass of FileBase, strong typed for a directory
class Directory : public detail::FileCommon<Directory>
{
public:
  using detail::FileCommon<Directory>::FileCommon;

  /// Sentinel value that can be used as a "dirfd" in various functions to use the current directory as the root.
  /// This is especially useful as a "do not care" value when using an absolute path.
  static const int at_cwd;

  /// Attempt to create and open the directory with the given name relative to `dirfd`.
  /// This does not support recursive directory creation, so if `name` is a path (contains "/") and the parent directory
  /// (everything before "/") doesn't exist then it will fail. It is not an error if the directory already exists.
  /// @param dirfd the descriptor of the directory to create in, or at_cwd for the current directory
  /// @param name the name of the directory to open/create.  will fail if a name contains a '/ and doesn't exist
  static jewels::expected<filesystem::Directory, ErrorCode> create_open(int dirfd, std::string_view name);

  /// Recursively calls `create_open(dirfd, name)` to create and open the directory '$dirfd/name1/name2/nameN'
  /// See that function's doc for details
  /// @param dirfd the descriptor of the directory to create in, or at_cwd for the current directory
  /// @param name... the names of the directories to open/create
  template <typename... Args>
  static jewels::expected<filesystem::Directory, ErrorCode>
  create_open(int dirfd, std::string_view name1, std::string_view name2, Args... names);

  /// Read through the directory and invoke the callback on every entry.  The Callback signature is:
  ///  jewels::expected<bool, ErrorCode> processor(int dirfd, const char* name, uint8_t type);
  /// Where
  ///  dirfd is the descriptor of this directory (can be used for `openat` calls)
  ///  name is the entry name as a string_view
  ///  type is the type of the file (may not be useful, see `man 2 getdents`)
  /// The callback should return `true` to continue processing or `false` to break (without error).  If the callback
  /// returns an error, processing will be stopped and that error will be returned
  jewels::expected<void, ErrorCode> process(auto&& processor);

protected:
  friend detail::FileCommon<Directory>;

  /// Extra flags to set when opening
  static const int open_flags;

  /// Reads the directory contents into the buffer, returning the number of bytes received.
  /// This wraps syscall(SYS_getdents64, ...) as is lower level than the normal POSIX API with the benefit that it can
  /// be done without allocations and the other overhead. Note that this updates the kernel's read cursor for the
  /// descriptor
  jewels::expected<size_t, ErrorCode> read_entries(std::span<std::byte> buffer);
};

} // namespace jewels::filesystem

#include "jewels/filesystem/file.inl"
