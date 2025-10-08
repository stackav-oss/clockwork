// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/filesystem/filesystem.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"

#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <iterator> // IWYU pragma: keep
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace jewels::filesystem::testing
{

/// File utility error injection wrapper
///
/// This class wraps the Filesystem class and provides hooks for error injection testing
class FilesystemWrapper
{
public:
  /// Structure used to keep track of injected error state
  struct InjectedErrorState
  {
    /// Error to inject
    ErrorCode error;

    /// Number of calls to skip before injecting the error
    size_t skip_count{};
  };

  /// Structure used to store a range of bytes for error injection
  struct InjectedErrorRange
  {
    /// First file offset in the range
    size_t start_offset{};

    /// Last file offset in the range
    size_t end_offset{};
  };

  /// Constructor
  /// @param[in] memory_resource Memory resource used to allocate memory
  explicit FilesystemWrapper(jewels::memory::MemoryResource memory_resource);

  ~FilesystemWrapper() noexcept = default;

  FilesystemWrapper(const FilesystemWrapper&) noexcept = default;
  FilesystemWrapper& operator=(const FilesystemWrapper&) noexcept = default;
  FilesystemWrapper(FilesystemWrapper&&) noexcept = default;
  FilesystemWrapper& operator=(FilesystemWrapper&&) noexcept = default;

  /// Set the error message verbosity
  /// @param[in] verbosity Error message verbosity
  void set_verbosity(Filesystem::ErrorVerbosity verbosity) noexcept;

  /// Open a file
  /// @param[in] file_path File path
  /// @param[in] mode_flags Mode flags (O_CLOEXEC is added automatically)
  /// @param[in] perms File permissions
  /// @return File descriptor or error code on failure
  [[nodiscard]] jewels::expected<FileDescriptor, ErrorCode>
  open(std::string_view file_path, int32_t mode_flags = O_RDONLY, uint32_t perms = Filesystem::default_file_perms);

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

  /// Get the size of a file
  /// @param[in] file_path File path
  /// @return File size or error code on failure
  [[nodiscard]] jewels::expected<size_t, ErrorCode> get_size(std::string_view file_path);

  /// Get the size of a file
  /// @param[in] file_desc File descriptor
  /// @return File size or error code on failure
  [[nodiscard]] jewels::expected<size_t, ErrorCode> get_size(const FileDescriptor& file_desc);

  /// Get the last write time of a file
  /// @param[in] file_desc File descriptor
  /// @return Last write time
  [[nodiscard]] jewels::expected<jewels::time::SyncTime, ErrorCode>
  get_last_write_time(const FileDescriptor& file_desc);

  /// Get the last write time of a file
  /// @param[in] file_path File path
  /// @return Last write time
  [[nodiscard]] jewels::expected<jewels::time::SyncTime, ErrorCode> get_last_write_time(std::string_view file_path);

  /// Set the last write time of a file
  /// @param[in] file_path File path
  /// @param[in] time_to_set Time to set
  /// @return Error code on failure
  [[nodiscard]] jewels::expected<void, ErrorCode>
  set_last_write_time(std::string_view file_path, jewels::time::SyncTime time_to_set);

  /// Set the last write time of a file
  /// @param[in] file_desc File descriptor
  /// @param[in] time_to_set Time to set
  /// @return Error code on failure
  [[nodiscard]] jewels::expected<void, ErrorCode>
  set_last_write_time(const FileDescriptor& file_desc, jewels::time::SyncTime time_to_set);

  /// Rename a file
  /// @param[in] old_path Old file path
  /// @param[in] new_path New file path
  /// @return Error code on failure
  [[nodiscard]] jewels::expected<void, ErrorCode> rename(std::string_view old_path, std::string_view new_path);

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

  /// Unlink a file
  /// @param[in] path File path
  /// @return Error code on failure
  [[nodiscard]] jewels::expected<void, ErrorCode> unlink(std::string_view path);

  /// Optionally creates a file if it does not exist, updates its last write time if it does.  Parent directories are
  /// also created as needed.
  /// @param[in] file_path File path
  /// @return Error code on failure
  [[nodiscard]] jewels::expected<void, ErrorCode> touch(std::string_view file_path);

  /// Creates a new temporary directory in the filesystem, creating parent directories as needed
  /// @return expected containing the path if the temporary directory was created; on failure returns an error code
  /// indicating the reason for failure
  [[nodiscard]] jewels::expected<filesystem::Path, ErrorCode>
  create_temporary_directory(std::optional<filesystem::Path> parent_path = std::nullopt);

  /// Creates a new temporary directory in the filesystem, creating parent directories as needed
  /// @return expected containing the path if the temporary directory was created; on failure returns an error code
  /// indicating the reason for failure
  [[nodiscard]] jewels::expected<std::pair<filesystem::Path, filesystem::FileDescriptor>, ErrorCode>
  create_temporary_file(std::optional<filesystem::Path> parent_path = std::nullopt);

  /// Copy a file
  /// @param[in] old_path Old file path
  /// @param[in] new_path New file path
  /// @return System error on failure
  [[nodiscard]] jewels::expected<void, ErrorCode> copy_file(std::string_view old_path, std::string_view new_path);

  /// Create a directory
  /// @param[in] path Directory path
  /// @return Error code on failure
  [[nodiscard]] jewels::expected<void, ErrorCode>
  create_directory(std::string_view path, uint32_t perms = Filesystem::default_directory_perms);

  /// Create a directory for every element int the path that doesn't exist
  /// @param[in] path Directory path
  /// @param[in] perms Directory permissions
  /// @return System error on failure
  [[nodiscard]] jewels::expected<void, ErrorCode>
  create_directories(std::string_view path, uint32_t perms = Filesystem::default_directory_perms);

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
  /// @param[in] filter_fn Filter function
  /// @return Directory entry names sorted in lexical order
  template <typename FilterFunctionType>
  [[nodiscard]] jewels::expected<std::pmr::vector<filesystem::Path>, ErrorCode>
  read_directory(std::string_view path, FilterFunctionType&& filter_fn);

  /// Read the directory entry names from a directory using a filter to select the names to be returned
  ///
  /// Fileter function is passed by const reference
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
  /// Fileter function is passed by const reference
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
  [[nodiscard]] jewels::expected<Filesystem::SpaceInformation, ErrorCode> get_space_information(std::string_view path);

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

  /// Inject an error in a future call to copy_file
  /// @param[in] error_code Error code to inject
  /// @param[in] skip_count Number of calls to skip before injecting the error
  void inject_copy_file_error(int32_t error_code, size_t skip_count = 0U);

  /// Inject an error in a future call to touch
  /// @param[in] error_code Error code to inject
  /// @param[in] skip_count Number of calls to skip before injecting the error
  void inject_touch_error(int32_t error_code, size_t skip_count = 0U);

  /// Inject an error in a future call to create_temporary_directory
  /// @param[in] error_code Error code to inject
  /// @param[in] skip_count Number of calls to skip before injecting the error
  void inject_create_temporary_directory_error(int32_t error_code, size_t skip_count = 0U);

  /// Inject an error in a future call to create_temporary_file
  /// @param[in] error_code Error code to inject
  /// @param[in] skip_count Number of calls to skip before injecting the error
  void inject_create_temporary_file_error(int32_t error_code, size_t skip_count = 0U);

  /// Inject an error in a future call to open
  /// @param[in] error_code Error code to inject
  /// @param[in] skip_count Number of calls to skip before injecting the error
  void inject_open_error(int32_t error_code, size_t skip_count = 0U);

  /// Inject an error in a future call to read
  /// @param[in] error_code Error code to inject
  /// @param[in] skip_count Number of calls to skip before injecting the error
  void inject_read_error(int32_t error_code, size_t skip_count = 0U);

  /// Inject an error in a future call to write
  /// @param[in] error_code Error code to inject
  /// @param[in] skip_count Number of calls to skip before injecting the error
  void inject_write_error(int32_t error_code, size_t skip_count = 0U);

  /// Inject an error in a future call to lseek
  /// @param[in] error_code Error code to inject
  /// @param[in] skip_count Number of calls to skip before injecting the error
  void inject_lseek_error(int32_t error_code, size_t skip_count = 0U);

  /// Inject I/O errors in reads that overlap a range of bytes
  /// @param[in] offset Offset to the start of the range
  /// @param[in] length Length of the range
  void inject_read_io_errors(size_t offset, size_t length);

  /// Inject an error in a future call to stat a file
  /// @param[in] error_code Error code to inject
  /// @param[in] skip_count Number of calls to skip before injecting the error
  void inject_stat_error(int32_t error_code, size_t skip_count = 0U);

  /// Inject an error in a future call to rename
  /// @param[in] error_code Error code to inject
  /// @param[in] skip_count Number of calls to skip before injecting the error
  void inject_rename_error(int32_t error_code, size_t skip_count = 0U);

  /// Inject an error in a future call to unlink
  /// @param[in] error_code Error code to inject
  /// @param[in] skip_count Number of calls to skip before injecting the error
  void inject_unlink_error(int32_t error_code, size_t skip_count = 0U);

  /// Inject an error in a future call to mkdir
  /// @param[in] error_code Error code to inject
  /// @param[in] skip_count Number of calls to skip before injecting the error
  void inject_mkdir_error(int32_t error_code, size_t skip_count = 0U);

  /// Inject an error in a future call to symlink
  /// @param[in] error_code Error code to inject
  /// @param[in] skip_count Number of calls to skip before injecting the error
  void inject_symlink_error(int32_t error_code, size_t skip_count = 0U);

  /// Inject an error in a future call to readlink
  /// @param[in] error_code Error code to inject
  /// @param[in] skip_count Number of calls to skip before injecting the error
  void inject_readlink_error(int32_t error_code, size_t skip_count = 0U);

  /// Inject an error in a future call to readdir
  /// @param[in] error_code Error code to inject
  /// @param[in] skip_count Number of calls to skip before injecting the error
  void inject_readdir_error(int32_t error_code, size_t skip_count = 0U);

  /// Inject an error in a future call to remove/remove_all
  /// @param[in] error_code Error code to inject
  /// @param[in] skip_count Number of calls to skip before injecting the error
  void inject_remove_error(int32_t error_code, size_t skip_count = 0U);

private:
  /// Test whether an error should be injected
  /// The injected error state is set to nullopt after an error is injected
  /// @param[in,out] maybe_injected_error_state
  /// @return Injected system error if an error should be injected
  [[nodiscard]] static jewels::expected<void, ErrorCode>
  check_error_injection_state(std::optional<FilesystemWrapper::InjectedErrorState>& maybe_injected_error_state);

  /// Memory resource used to allocate after initialization
  jewels::memory::MemoryResource memory_resource_;

  /// Wrapped file utility instance
  Filesystem wrapped_;

  /// Error injection state for copy_file
  std::optional<InjectedErrorState> maybe_inject_create_temporary_directory_error_state_;

  /// Error injection state for copy_file
  std::optional<InjectedErrorState> maybe_inject_create_temporary_file_error_state_;

  /// Error injection state for copy_file
  std::optional<InjectedErrorState> maybe_inject_copy_file_error_state_;

  /// Error injection state for touch
  std::optional<InjectedErrorState> maybe_inject_touch_error_state_;

  /// Error injection state for open
  std::optional<InjectedErrorState> maybe_inject_open_error_state_;

  /// Error injection state for read
  std::optional<InjectedErrorState> maybe_inject_read_error_state_;

  /// File range to inject EIO for calls to read
  std::optional<InjectedErrorRange> maybe_read_io_error_range_;

  /// Error injection state for write
  std::optional<InjectedErrorState> maybe_inject_write_error_state_;

  /// Error injection state for lseek
  std::optional<InjectedErrorState> maybe_inject_lseek_error_state_;

  /// Error injection state for stat
  std::optional<InjectedErrorState> maybe_inject_stat_error_state_;

  /// Error injection state for rename
  std::optional<InjectedErrorState> maybe_inject_rename_error_state_;

  /// Error injection state for unlink
  std::optional<InjectedErrorState> maybe_inject_unlink_error_state_;

  /// Error injection state for mkdir
  std::optional<InjectedErrorState> maybe_inject_mkdir_error_state_;

  /// Error injection state for symlink
  std::optional<InjectedErrorState> maybe_inject_symlink_error_state_;

  /// Error injection state for readlink
  std::optional<InjectedErrorState> maybe_inject_readlink_error_state_;

  /// Error injection state for readdir
  std::optional<InjectedErrorState> maybe_inject_readdir_error_state_;

  /// Error injection state for remove/remove_all
  std::optional<InjectedErrorState> maybe_inject_remove_error_state_;
};

} // namespace jewels::filesystem::testing

#include "jewels/testing/filesystem_wrapper.inl"
