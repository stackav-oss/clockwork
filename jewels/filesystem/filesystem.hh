// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"

#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <span>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <sys/types.h>
#include <vector>

namespace jewels::filesystem
{

/// File utilities intended for onboard use
///
/// This class is not using std::filesystem intentionally. The std::filesystem classes
/// are not allocator aware and must not be used in onboard code.
///
/// There is an error injection wrapper for this class in jewels::testing.
class Filesystem
{
public:
  /// Error message verbosity
  enum class ErrorVerbosity : uint8_t
  {
    // Don't print messages to cerr on failures
    off,
    // Print messages to cerr on failures
    verbose,
  };

  /// Linux directory entry structure
  ///
  /// This needs to be defined here because there is no glibc wrapper for getdents64
  struct LinuxDirent64
  {
    /// 64-bit inode number
    ino64_t d_ino;
    /// 64-bit offset to next structure
    off64_t d_off;
    /// Size of this dirent
    uint16_t d_reclen;
    /// File type
    unsigned char d_type;
    /// Filename (null-terminated)
    char d_name[1]; // NOLINT(modernize-avoid-c-arrays)
  };

  /// Struct used to return filesystem space information
  struct SpaceInformation
  {
    /// Total capacity in bytes
    std::size_t capacity{};
    /// Free space in bytes
    std::size_t free{};
    /// Available space in bytes
    std::size_t available{};
  };

  /// Default file permissions
  static constexpr uint32_t default_file_perms = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;

  /// Default directory permissions
  static constexpr uint32_t default_directory_perms = S_IRWXU | S_IRWXG | S_IRWXO;

  /// Constructor
  /// @param[in] memory_resource Memory resource used to allocate memory
  explicit Filesystem(jewels::memory::MemoryResource memory_resource);

  ~Filesystem() noexcept = default;

  Filesystem(const Filesystem&) noexcept = default;
  Filesystem& operator=(const Filesystem&) noexcept = default;
  Filesystem(Filesystem&&) noexcept = default;
  Filesystem& operator=(Filesystem&&) noexcept = default;

  /// Set the error message verbosity
  /// @param[in] verbosity Error message verbosity
  void set_verbosity(ErrorVerbosity verbosity) noexcept;

  /// Open a file
  /// @param[in] file_path File path
  /// @param[in] mode_flags Mode flags (O_CLOEXEC is added automatically)
  /// @param[in] perms File permissions
  /// @return File descriptor or error code on failure
  [[nodiscard]] jewels::expected<FileDescriptor, ErrorCode>
  open(std::string_view file_path, int32_t mode_flags = O_RDONLY, uint32_t perms = default_file_perms);

  /// Get the size of a file
  /// @param[in] file_path File path
  /// @return File size or error code on failure
  [[nodiscard]] jewels::expected<size_t, ErrorCode> get_size(std::string_view file_path);

  /// Get the size of a file
  /// @param[in] file_desc File descriptor
  /// @return File size or error code on failure
  [[nodiscard]] jewels::expected<size_t, ErrorCode> get_size(const FileDescriptor& file_desc);

  /// Get the last write time of a file
  /// @param[in] file_path File path
  /// @return Last write time or error code on failure
  [[nodiscard]] jewels::expected<jewels::time::SyncTime, ErrorCode> get_last_write_time(std::string_view file_path);

  /// Get the last write time of a file
  /// @param[in] file_desc File descriptor
  /// @return Last write time or error code on failure
  [[nodiscard]] jewels::expected<jewels::time::SyncTime, ErrorCode>
  get_last_write_time(const FileDescriptor& file_desc);

  /// Creates a new file in the filesystem, creating parent directories as needed, or updates the modification time
  //  if the file already exists
  /// @param[in] path Path
  /// @return expected containing void if the file/dir was successfully created or had its modification time updated;
  /// on failure returns an error code indicating the reason for failure
  [[nodiscard]] jewels::expected<void, ErrorCode> touch(std::string_view path);

  /// Creates a new temporary directory in the filesystem, creating parent directories as needed
  /// @return expected containing the path if the temporary directory was created; on failure returns an error code
  /// indicating the reason for failure
  [[nodiscard]] jewels::expected<filesystem::Path, ErrorCode> create_temporary_directory();

  /// Set the last write time of a file
  /// @param[in] file_path File path
  /// @param[in] time_to_set Time to set
  /// @return Error code on failure
  [[nodiscard]] jewels::expected<void, ErrorCode>
  set_last_write_time(std::string_view file_path, jewels::time::SyncTime time_to_set);

  /// Get the last write time of a file
  /// @param[in] file_desc File descriptor
  /// @param[in] time_to_set Time to set
  /// @return Error code on failure
  [[nodiscard]] jewels::expected<void, ErrorCode>
  set_last_write_time(const FileDescriptor& file_desc, jewels::time::SyncTime time_to_set);

  /// Read from a file descriptor
  /// @param[in] file_desc File descriptor
  /// @param[in] data Data buffer span
  /// @return Number of bytes read or error code on failure
  [[nodiscard]] jewels::expected<size_t, ErrorCode> read(const FileDescriptor& file_desc, std::span<std::byte> data);

  /// Read from a file descriptor at a specified offset
  /// @param[in] file_desc File descriptor
  /// @param[in] offset File offset
  /// @param[in] data Data buffer span
  /// @return Number of bytes read or error code on failure
  [[nodiscard]] jewels::expected<size_t, ErrorCode>
  read(const FileDescriptor& file_desc, size_t offset, std::span<std::byte> data);

  /// Write to a file descriptor
  /// @param[in] file_desc File descriptor
  /// @param[in] data Data buffer span
  /// @return Number of bytes written or error code on failure
  [[nodiscard]] jewels::expected<size_t, ErrorCode>
  write(const FileDescriptor& file_desc, std::span<const std::byte> data);

  /// Write to a file descriptor at a specified offset
  /// @param[in] file_desc File descriptor
  /// @param[in] offset File offset
  /// @param[in] data Data buffer span
  /// @return Number of bytes written or error code on failure
  [[nodiscard]] jewels::expected<size_t, ErrorCode>
  write(const FileDescriptor& file_desc, size_t offset, std::span<const std::byte> data);

  /// Set the file offset
  /// @param[in] file_desc File descriptor
  /// @param[in] offset File offset
  /// @return Error code on failure
  [[nodiscard]] jewels::expected<void, ErrorCode> set_position(const FileDescriptor& file_desc, size_t offset);

  /// Get the file offset
  /// @param[in] file_desc File descriptor
  /// @return File offset or code on failure
  [[nodiscard]] jewels::expected<size_t, ErrorCode> get_position(const FileDescriptor& file_desc);

  /// Rename a file
  /// @param[in] old_path Old file path
  /// @param[in] new_path New file path
  /// @param[in] copy_delete_cross_filesystem If true, copy and delete the file if it is on a different filesystem
  /// @return System error on failure
  [[nodiscard]] jewels::expected<void, ErrorCode>
  rename(std::string_view old_path, std::string_view new_path, bool copy_delete_cross_filesystem = false);

  /// Copy a file
  /// @param[in] source_path Path to file to copy
  /// @param[in] destination_path Path to where we copy the source file
  /// @return System error on failure
  [[nodiscard]] jewels::expected<void, ErrorCode>
  copy_file(std::string_view source_path, std::string_view destination_path);

  /// Unlink a file
  /// @param[in] path File path
  /// @return System error on failure
  [[nodiscard]] jewels::expected<void, ErrorCode> unlink(std::string_view path);

  /// Create a directory
  /// @param[in] path Directory path
  /// @param[in] perms Directory permissions
  /// @return System error on failure
  [[nodiscard]] jewels::expected<void, ErrorCode>
  create_directory(std::string_view path, uint32_t perms = default_directory_perms);

  /// Create a directory for every element int the path that doesn't exist
  /// @param[in] path Directory path
  /// @param[in] perms Directory permissions
  /// @return System error on failure
  [[nodiscard]] jewels::expected<void, ErrorCode>
  create_directories(std::string_view path, uint32_t perms = default_directory_perms);

  /// Create a symbolic link named link_path that contains the string target_path
  /// @param[in] target_path Target path
  /// @param[in] link_path Link path
  /// @return Error condition on failure
  [[nodiscard]] jewels::expected<void, ErrorCode>
  create_symlink(std::string_view target_path, std::string_view link_path);

  /// Read a symbolic link
  /// @param[in] link_path Symbolic link path
  /// @return Link contents or error code on failure
  [[nodiscard]] jewels::expected<std::pmr::string, ErrorCode> read_symlink(std::string_view link_path);

  /// Test whether a path exists
  /// @param[in] path Path
  /// @return True if the path exists or error code on failure
  [[nodiscard]] jewels::expected<bool, ErrorCode> exists(std::string_view path);

  /// Test whether a path is a directory
  /// @param[in] path Path
  /// @return True if the path is a directory or error code on failure
  [[nodiscard]] jewels::expected<bool, ErrorCode> is_directory(std::string_view path);

  /// Test whether a path is a regular file
  /// @param[in] path Path
  /// @return True if the path is a regular file or error code on failure
  [[nodiscard]] jewels::expected<bool, ErrorCode> is_regular_file(std::string_view path);

  /// Read the directory entry names from a directory
  /// @param[in] path Directory path
  /// @return Directory entry names sorted in lexical order
  [[nodiscard]] jewels::expected<std::pmr::vector<filesystem::Path>, ErrorCode> read_directory(std::string_view path);

  /// Read the directory entry names from a directory using a filter to select the names to be returned
  ///
  /// Filter function can be either an lvalue or an rvalue
  ///
  /// @tparam FilterFunctionType Filter function type
  /// @param[in] path Directory path
  /// @param[in] filter_fn
  /// @return Directory entry names sorted in lexical order
  template <typename FilterFunctionType>
  [[nodiscard]] jewels::expected<std::pmr::vector<filesystem::Path>, ErrorCode>
  read_directory(std::string_view path, FilterFunctionType&& filter_fn);

  /// Read the directory entry names from a directory using a filter to select the names to be returned
  ///
  /// Filter function is passed by const reference
  ///
  /// @tparam FilterFunctionType Filter function type
  /// @param[in] path Directory path
  /// @param[in] filter_fn
  /// @return Directory entry names sorted in lexical order
  template <typename FilterFunctionType>
  [[nodiscard]] jewels::expected<std::pmr::vector<filesystem::Path>, ErrorCode>
  read_directory(std::string_view path, const FilterFunctionType& filter_fn);

  /// Read the directory entry names from a directory recursively
  /// @param[in] path Directory path
  /// @param[in] ignore_permission_denied Flag to ignore permission denied errors
  /// @return Directory entry names sorted in lexical order
  [[nodiscard]] jewels::expected<std::pmr::vector<filesystem::Path>, ErrorCode>
  read_directories(std::string_view path, bool ignore_permission_denied = true);

  /// Read the directory entry names from a directory recursively using a filter to select the names to be returned
  ///
  /// Filter function can be either an lvalue or an rvalue
  ///
  /// @tparam FilterFunctionType Filter function type
  /// @param[in] path Directory path
  /// @param[in] filter_fn
  /// @param[in] ignore_permission_denied True to ignore permission denied errors
  /// @return Directory entry names sorted in lexical order
  template <typename FilterFunctionType>
  [[nodiscard]] jewels::expected<std::pmr::vector<filesystem::Path>, ErrorCode>
  read_directories(std::string_view path, FilterFunctionType&& filter_fn, bool ignore_permission_denied = true);

  /// Read the directory entry names from a directory recursively using a filter to select the names to be returned
  ///
  /// Filter function is passed by const reference
  ///
  /// @tparam FilterFunctionType Filter function type
  /// @param[in] path Directory path
  /// @param[in] filter_fn
  /// @param[in] ignore_permission_denied True to ignore permission denied errors
  /// @return Directory entry names sorted in lexical order
  template <typename FilterFunctionType>
  [[nodiscard]] jewels::expected<std::pmr::vector<filesystem::Path>, ErrorCode>
  read_directories(std::string_view path, const FilterFunctionType& filter_fn, bool ignore_permission_denied = true);

  /// Get the filesystem space information for a path
  /// @param[in] path Path
  /// @return Filesystem space information or error code on failure
  [[nodiscard]] jewels::expected<SpaceInformation, ErrorCode> get_space_information(std::string_view path);

  /// Removes the file or empty directory pointed to by path
  /// @param[in] path File or directory path
  /// @return expected containing void if the file/dir exists and was removed; on failure returns an error code
  /// indicating the reason for failure
  [[nodiscard]] jewels::expected<void, ErrorCode> remove(std::string_view path);

  /// Recursively removes the contents of the path (and its contents if it is a directory), then deletes the path
  /// @param[in] path File or directory path
  /// @return expected containing the number of files and directories deleted; on failure returns an error code
  /// indicating the reason for failure.
  [[nodiscard]] jewels::expected<size_t, ErrorCode> remove_all(std::string_view path);

private:
  /// Structure used to keep track of the directories when reading directories recursively
  struct DirectoryToRead
  {
    /// Constructor
    DirectoryToRead(filesystem::Path directory_path_in, filesystem::Path parent_path_in) noexcept;

    /// Directory path
    filesystem::Path directory_path;

    /// Parent prefix to apply to the directory entries
    filesystem::Path parent_path;
  };

  /// Read the directory entry names from a directory using a filter to select the names to be returned
  /// @tparam FilterFunctionType Filter function type
  /// @param[in] path Directory path
  /// @param[in] parent_path Parent path name to prepend to the returned file names
  /// @param[in[ ignore_permission_denied Flag to ignore permission denied errors when reading directories
  /// @param[out] entries Vector that contains the entries read from the directory
  /// @param[in] filter_fn
  /// @return Directory entry names sorted in lexical order
  template <typename FilterFunctionType>
  [[nodiscard]] jewels::expected<void, ErrorCode> read_directory_impl(
    std::string_view path,
    std::string_view parent_path,
    const FilterFunctionType& filter_fn,
    bool ignore_permission_denied,
    std::pmr::vector<filesystem::Path>& entries);

  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;

  /// Error message verbosity
  ErrorVerbosity verbosity_{ErrorVerbosity::off};
};

} // namespace jewels::filesystem

#include "jewels/filesystem/filesystem.inl"
