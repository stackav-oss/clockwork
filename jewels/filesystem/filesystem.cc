// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/filesystem/filesystem.hh"

#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/scope_guard/scope_guard.hh"
#include "jewels/std/expected.hh"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <iterator>
#include <list>
#include <memory_resource>
#include <ranges>
#include <string>
#include <sys/file.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

namespace jewels::filesystem
{

namespace
{

/// Get the stats for a file descriptor
/// @param[in] file_desc File descriptor
/// @param[in] verbosity Error verbosity
/// @return Stats or error condition on failure
[[nodiscard]] jewels::expected<struct stat, ErrorCode>
stat_file(const FileDescriptor& file_desc, Filesystem::ErrorVerbosity verbosity)
{
  struct stat statbuf{};
  if (const auto ret = ::fstat(*file_desc, &statbuf); ret < 0)
  {
    const auto error = make_error_code(errno);
    if (errno != ENOENT && verbosity != Filesystem::ErrorVerbosity::off)
    {
      jewels::log_cerr_error("Failed to stat file descriptor {}: {}", *file_desc, error.message());
    }
    return jewels::unexpected(error);
  }
  return statbuf;
}

/// Get the stats for a path
/// @param[in] path Path
/// @param[in] memory_resource Memory resource
/// @param[in] verbosity Error verbosity
/// @return Stats or error condition on failure
[[nodiscard]] jewels::expected<struct stat, ErrorCode>
stat_file(std::string_view path, jewels::memory::MemoryResource memory_resource, Filesystem::ErrorVerbosity verbosity)
{
  struct stat statbuf{};
  const std::pmr::string path_str{path, memory_resource};
  if (const auto ret = ::stat(path_str.c_str(), &statbuf); ret < 0)
  {
    const auto error = make_error_code(errno);
    if (errno != ENOENT && verbosity != Filesystem::ErrorVerbosity::off)
    {
      jewels::log_cerr_error("Failed to stat '{}': {}", path, error.message());
    }
    return jewels::unexpected(error);
  }
  return statbuf;
}

[[nodiscard]] jewels::expected<std::pmr::string, ErrorCode> prepare_template_for_temporary(
  std::optional<filesystem::Path> parent_path,
  jewels::memory::MemoryResource memory_resource,
  const Filesystem& filesystem)
{
  filesystem::Path template_path{"/tmp", memory_resource};
  if (parent_path.has_value() && parent_path->is_absolute())
  {
    template_path = parent_path.value();
  }
  else
  {
    const char* session_dir =
      std::getenv("XDG_SESSION_DIR"); // NOLINT(concurrency-mt-unsafe) Thread-unsafety documented in api
    if (session_dir != nullptr && std::strlen(session_dir) > 0)
    {
      template_path = filesystem::Path{session_dir, memory_resource};
    }
  }
  if (parent_path.has_value() && !parent_path->is_absolute())
  {
    template_path /= parent_path->string_view();
  }
  constexpr auto only_me = 0700;
  if (auto possible_create_directories = filesystem.create_directories(template_path, only_me);
      !possible_create_directories)
  {
    return jewels::unexpected(possible_create_directories.error());
  }
  template_path /= "XXXXXX";
  return std::pmr::string{template_path.string_view(), memory_resource};
}

/// Compute the number of bytes to transfer in a single syscall.
/// @param[in] remaining Number of bytes remaining to transfer
/// @param[in] block_size Maximum transfer size requested by the caller
/// @return Number of bytes to transfer, clamped to [1, min(block_size, SSIZE_MAX, remaining)]
[[nodiscard]] size_t clamp_chunk_size(off_t remaining, size_t block_size)
{
  // This should always be true, but this is enforcing that to be true.
  const auto positive_remaining = static_cast<size_t>(std::max(remaining, off_t{1}));
  return std::min({positive_remaining, block_size, static_cast<size_t>(SSIZE_MAX)});
}

/// Copy the file from source to destination using copy_file_range() at the given offsets.
/// @param[in] source Source file descriptor
/// @param[in] destination Destination file descriptor
/// @param[in] file_size Total size of the source file in bytes
/// @param[in,out] offset_source Offset in the source file
/// @param[in,out] offset_destination Offset in the destination file
/// @param[in] block_size Maximum number of bytes to transfer per call
/// @return ErrorCode on failure. Note that EXDEV and EOPNOTSUPP are not treated as errors, but rather indications that
/// copy_file_range() is not supported, and the caller should fall back to another method.
[[nodiscard]] jewels::expected<void, ErrorCode> copy_with_file_range(
  const FileDescriptor& source,
  const FileDescriptor& destination,
  off_t file_size,
  off_t& offset_source,
  off_t& offset_destination,
  size_t block_size)
{
  while (offset_source < file_size)
  {
    const auto remaining = file_size - offset_source;
    const auto next_block_size = clamp_chunk_size(remaining, block_size);
    const auto written =
      ::copy_file_range(*source, &offset_source, *destination, &offset_destination, next_block_size, 0);
    if (written == -1)
    {
      const auto error = jewels::filesystem::make_error_code(errno);
      // These cases aren't part of the normal Linux API, but EAGAIN has been reported as a return, and EINTR is part of
      // the BSD interface. Neither of these are fatal errors, and do not indicate a lack of support for
      // copy_file_range, so we will just return success with 0 progress.
      if (error == std::errc::interrupted || error == std::errc::resource_unavailable_try_again)
      {
        continue;
      }
      return jewels::unexpected(error);
    }
    // 0 indicates copying is done, even if we haven't reached the expected file size. This can happen if the file is
    // modified while copying
    if (written == 0)
    {
      break;
    }
  }
  return {};
}

/// Copy bytes from source to destination using sendfile(), starting from the beginning of the source file.
/// @param[in] source Source file descriptor
/// @param[in] destination Destination file descriptor
/// @param[in] file_size Total size of the source file in bytes
/// @param[in] block_size Maximum number of bytes to transfer per call
/// @return Error condition on failure
[[nodiscard]] jewels::expected<void, ErrorCode>
copy_with_sendfile(const FileDescriptor& source, const FileDescriptor& destination, off_t file_size, size_t block_size)
{
  off_t offset_source{0};
  while (offset_source < file_size)
  {
    const auto remaining = file_size - offset_source;
    const auto next_block_size = clamp_chunk_size(remaining, block_size);
    const auto written = ::sendfile(*destination, *source, &offset_source, next_block_size);
    if (written == -1)
    {
      const auto error = make_error_code(errno);
      if (error != std::errc::interrupted && error != std::errc::resource_unavailable_try_again)
      {
        return jewels::unexpected(error);
      }
      continue;
    }
  }
  return {};
}

/// Copy bytes from source to destination using copy_file_range(), starting at the given offsets.
/// @param[in] source Source file descriptor
/// @param[in] destination Destination file descriptor
/// @param[in] file_size Total size of the source file in bytes
/// @param[in] block_size Maximum number of bytes to transfer per call
/// @return Error condition on failure
[[nodiscard]] jewels::expected<void, ErrorCode>
copy_with_fallback(const FileDescriptor& source, const FileDescriptor& destination, off_t file_size, size_t block_size)
{
  if (file_size == 0)
  {
    return {};
  }

  off_t offset_src{0};
  off_t offset_dst{0};
  // The first attempt will either make progress, or fail because it isn't supported. If it fails, we fall back to
  // sendfile
  auto copy_result = copy_with_file_range(source, destination, file_size, offset_src, offset_dst, block_size);

  // If we get an error that isn't indicative of copy_file_range being unsupported, return the error.
  if (
    !copy_result && copy_result.error() != std::errc::cross_device_link &&
    copy_result.error() != std::errc::operation_not_supported)
  {
    return jewels::unexpected(copy_result.error());
  }
  return copy_with_sendfile(source, destination, file_size, block_size);
}

} // namespace

Filesystem::DirectoryToRead::DirectoryToRead(
  filesystem::Path directory_path_in, filesystem::Path parent_path_in) noexcept
  : directory_path(std::move(directory_path_in)), parent_path(std::move(parent_path_in))
{
}

Filesystem::Filesystem(jewels::memory::MemoryResource memory_resource)
  : memory_resource_(std::move(memory_resource))
{
}

void Filesystem::set_verbosity(ErrorVerbosity verbosity) noexcept
{
  verbosity_ = verbosity;
}

[[nodiscard]] jewels::expected<FileDescriptor, ErrorCode>
Filesystem::open(std::string_view file_path, int32_t mode_flags, uint32_t perms) const
{
  const std::pmr::string file_path_str{file_path, memory_resource_};
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg, hicpp-signed-bitwise) Needed to use the open API
  FileDescriptor file_desc{::open(file_path_str.c_str(), mode_flags | O_CLOEXEC, perms)};
  if (!file_desc)
  {
    const auto err_code = make_error_code(errno);
    if (verbosity_ != ErrorVerbosity::off)
    {
      jewels::log_cerr_error("Failed to open '{}': {}", file_path, err_code.message());
    }
    return jewels::unexpected(err_code);
  }
  return {std::move(file_desc)};
}

[[nodiscard]] jewels::expected<size_t, ErrorCode> Filesystem::get_size(const FileDescriptor& file_desc) const
{
  const auto stat_result = stat_file(file_desc, verbosity_);
  if (!stat_result)
  {
    return jewels::unexpected(stat_result.error());
  }
  return static_cast<size_t>(stat_result->st_size);
}

[[nodiscard]] jewels::expected<size_t, ErrorCode> Filesystem::get_size(std::string_view file_path) const
{
  const auto stat_result = stat_file(file_path, memory_resource_, verbosity_);
  if (!stat_result)
  {
    return jewels::unexpected(stat_result.error());
  }
  return static_cast<size_t>(stat_result->st_size);
}

[[nodiscard]] jewels::expected<void, ErrorCode> Filesystem::touch(std::string_view path, uint32_t perms) const
{
  const filesystem::Path file_path{path, memory_resource_};
  if (is_directory(file_path.parent_path()) != true)
  {
    auto expected_create_dir = create_directories(file_path.parent_path());
    if (!expected_create_dir)
    {
      return expected_create_dir;
    }
  }
  auto expected_fd = open(file_path, O_WRONLY | O_CREAT, perms);
  if (!expected_fd)
  {
    return jewels::unexpected(expected_fd.error());
  }
  auto expected_set_last_write_time = set_last_write_time(*expected_fd, jewels::time::SyncClock::now());
  if (!expected_set_last_write_time)
  {
    return expected_set_last_write_time;
  }
  return {};
}

[[nodiscard]] jewels::expected<filesystem::Path, ErrorCode>
Filesystem::create_temporary_directory(std::optional<filesystem::Path> parent_path) const
{
  auto possible_created_template = prepare_template_for_temporary(std::move(parent_path), memory_resource_, *this);
  if (!possible_created_template)
  {
    return jewels::unexpected(possible_created_template.error());
  }
  if (::mkdtemp(possible_created_template->data()) == nullptr)
  {
    const auto error = make_error_code(errno);
    if (verbosity_ != Filesystem::ErrorVerbosity::off)
    {
      jewels::log_cerr_error(
        "Failed to create temporary directory '{}': {}", possible_created_template->data(), error.message());
    }
    return jewels::unexpected(error);
  }
  return filesystem::Path{*possible_created_template, memory_resource_};
}

[[nodiscard]] jewels::expected<std::pair<filesystem::Path, FileDescriptor>, ErrorCode>
Filesystem::create_temporary_file(std::optional<filesystem::Path> parent_path) const
{
  auto possible_created_template = prepare_template_for_temporary(std::move(parent_path), memory_resource_, *this);
  if (!possible_created_template)
  {
    return jewels::unexpected(possible_created_template.error());
  }
  auto possible_fd = ::mkstemp(possible_created_template->data());
  if (possible_fd == -1)
  {
    const auto error = make_error_code(errno);
    if (verbosity_ != Filesystem::ErrorVerbosity::off)
    {
      jewels::log_cerr_error(
        "Failed to create temporary file '{}': {}", possible_created_template->data(), error.message());
    }
    return jewels::unexpected(error);
  }
  return std::make_pair(filesystem::Path{*possible_created_template, memory_resource_}, FileDescriptor{possible_fd});
}

[[nodiscard]] jewels::expected<jewels::time::SyncTime, ErrorCode>
Filesystem::get_last_write_time(const FileDescriptor& file_desc) const
{
  const auto stat_result = stat_file(file_desc, verbosity_);
  if (!stat_result)
  {
    return jewels::unexpected(stat_result.error());
  }
  return jewels::time::SyncTime{
    std::chrono::seconds(stat_result->st_mtim.tv_sec) + std::chrono::nanoseconds(stat_result->st_mtim.tv_nsec)};
}

[[nodiscard]] jewels::expected<jewels::time::SyncTime, ErrorCode>
Filesystem::get_last_write_time(std::string_view file_path) const
{
  const auto stat_result = stat_file(file_path, memory_resource_, verbosity_);
  if (!stat_result)
  {
    return jewels::unexpected(stat_result.error());
  }
  return jewels::time::SyncTime{
    std::chrono::seconds(stat_result->st_mtim.tv_sec) + std::chrono::nanoseconds(stat_result->st_mtim.tv_nsec)};
}

[[nodiscard]] jewels::expected<void, ErrorCode>
Filesystem::set_last_write_time(const FileDescriptor& file_desc, jewels::time::SyncTime time_to_set) const
{
  const std::chrono::nanoseconds time_since_epoch = time_to_set.time_since_epoch();
  const auto time_s = std::chrono::duration_cast<std::chrono::seconds>(time_since_epoch);
  const auto time_ns = time_since_epoch - time_s;
  std::array times{
    ::timespec{.tv_sec = time_s.count(), .tv_nsec = time_ns.count()},
    ::timespec{.tv_sec = time_s.count(), .tv_nsec = time_ns.count()}};
  if (const auto ret = ::futimens(*file_desc, times.data()); ret < 0)
  {
    const auto error = make_error_code(errno);
    if (verbosity_ != ErrorVerbosity::off)
    {
      jewels::log_cerr_error("Failed to set write time on file descriptor {}: {}", *file_desc, error.message());
    }
    return jewels::unexpected(error);
  }
  return {};
}

[[nodiscard]] jewels::expected<void, ErrorCode>
Filesystem::set_last_write_time(std::string_view file_path, jewels::time::SyncTime time_to_set) const
{
  const auto open_result = open(file_path, O_RDWR);
  if (!open_result)
  {
    return jewels::unexpected(open_result.error());
  }
  return set_last_write_time(open_result.value(), time_to_set);
}

[[nodiscard]] jewels::expected<size_t, ErrorCode>
Filesystem::read(const FileDescriptor& file_desc, std::span<std::byte> data) const
{
  const auto ret = ::read(*file_desc, data.data(), data.size());
  if (ret < 0)
  {
    const auto error = make_error_code(errno);
    if (verbosity_ != ErrorVerbosity::off)
    {
      jewels::log_cerr_error("Failed to read from file descriptor {}: {}", *file_desc, error.message());
    }
    return jewels::unexpected(error);
  }
  return static_cast<size_t>(ret);
}

[[nodiscard]] jewels::expected<size_t, ErrorCode>
Filesystem::read(const FileDescriptor& file_desc, size_t offset, std::span<std::byte> data) const
{
  const auto ret = ::pread(*file_desc, data.data(), data.size(), static_cast<off_t>(offset));
  if (ret < 0)
  {
    const auto error = make_error_code(errno);
    if (verbosity_ != ErrorVerbosity::off)
    {
      jewels::log_cerr_error("Failed to read from file descriptor {}: {}", *file_desc, error.message());
    }
    return jewels::unexpected(error);
  }
  return static_cast<size_t>(ret);
}

[[nodiscard]] jewels::expected<size_t, ErrorCode>
Filesystem::write(const FileDescriptor& file_desc, std::span<const std::byte> data) const
{
  const auto ret = ::write(*file_desc, data.data(), data.size());
  if (ret < 0)
  {
    const auto error = make_error_code(errno);
    if (verbosity_ != ErrorVerbosity::off)
    {
      jewels::log_cerr_error("Failed to write to file descriptor {}: {}", *file_desc, error.message());
    }
    return jewels::unexpected(error);
  }
  return static_cast<size_t>(ret);
}

[[nodiscard]] jewels::expected<size_t, ErrorCode>
Filesystem::write(const FileDescriptor& file_desc, size_t offset, std::span<const std::byte> data) const
{
  const auto ret = ::pwrite(*file_desc, data.data(), data.size(), static_cast<off_t>(offset));
  if (ret < 0)
  {
    const auto error = make_error_code(errno);
    if (verbosity_ != ErrorVerbosity::off)
    {
      jewels::log_cerr_error("Failed to write to file descriptor {}: {}", *file_desc, error.message());
    }
    return jewels::unexpected(error);
  }
  return static_cast<size_t>(ret);
}

[[nodiscard]] jewels::expected<size_t, ErrorCode> Filesystem::get_position(const FileDescriptor& file_desc) const
{
  const auto ret = ::lseek(*file_desc, 0, SEEK_CUR);
  if (ret < 0)
  {
    const auto error = make_error_code(errno);
    if (verbosity_ != ErrorVerbosity::off)
    {
      jewels::log_cerr_error("Failed to get the position for file descriptor {}: {}", *file_desc, error.message());
    }
    return jewels::unexpected(error);
  }
  return static_cast<size_t>(ret);
}

[[nodiscard]] jewels::expected<void, ErrorCode>
Filesystem::set_position(const FileDescriptor& file_desc, size_t offset) const
{
  if (const auto ret = ::lseek(*file_desc, static_cast<ssize_t>(offset), SEEK_SET); ret < 0)
  {
    const auto error = make_error_code(errno);
    if (verbosity_ != ErrorVerbosity::off)
    {
      jewels::log_cerr_error("Failed to set the position for file descriptor {}: {}", *file_desc, error.message());
    }
    return jewels::unexpected(error);
  }
  return {};
}

[[nodiscard]] jewels::expected<void, ErrorCode>
Filesystem::copy_file(std::string_view source_path, std::string_view destination_path, size_t block_size) const
{
  const std::pmr::string source_path_str{source_path, memory_resource_};
  const std::pmr::string destination_path_str{destination_path, memory_resource_};
  const auto maybe_source = open(source_path_str, O_RDONLY);
  if (!maybe_source)
  {
    return jewels::unexpected(maybe_source.error());
  }
  const auto& source = *maybe_source;
  // Lock the file to flag that it shouldn't be modified while reading
  ::flock(*source, LOCK_SH);
  // Ensure the lock is always unlocked
  [[maybe_unused]] const ScopeGuard unlock_source{[&source] { ::flock(*source, LOCK_UN); }};

  const auto maybe_stat = stat_file(source, verbosity_);
  if (!maybe_stat)
  {
    return jewels::unexpected(maybe_stat.error());
  }

  constexpr mode_t permission_bits = S_IRWXU | S_IRWXG | S_IRWXO;
  const mode_t source_permissions = maybe_stat->st_mode & permission_bits;
  // Match source permissions, but also ensure we can write to it, otherwise this operation is a bit pointless
  const mode_t destination_permissions = source_permissions | S_IWUSR;

  // Create new or truncate existing at destination
  auto maybe_destination = open(destination_path_str, O_WRONLY | O_CREAT | O_TRUNC, destination_permissions);
  if (!maybe_destination)
  {
    return jewels::unexpected(maybe_destination.error());
  }
  auto& destination = *maybe_destination;
  // Lock the file to indicate it is being written to and shouldn't be modified.
  ::flock(*destination, LOCK_EX);
  [[maybe_unused]] const ScopeGuard unlock_destination{[&destination] { ::flock(*destination, LOCK_UN); }};

  if (block_size == 0 || block_size > static_cast<size_t>(SSIZE_MAX))
  {
    block_size = static_cast<size_t>(std::min(SSIZE_MAX, maybe_stat->st_size));
  }

  if (const auto copy_result = copy_with_fallback(source, destination, maybe_stat->st_size, block_size); !copy_result)
  {
    return jewels::unexpected(copy_result.error());
  }

  return {};
}

[[nodiscard]] jewels::expected<void, ErrorCode>
Filesystem::rename(std::string_view old_path, std::string_view new_path, bool copy_delete_cross_filesystem) const
{
  const std::pmr::string old_path_str{old_path, memory_resource_};
  const std::pmr::string new_path_str{new_path, memory_resource_};
  if (const auto ret = ::rename(old_path_str.c_str(), new_path_str.c_str()); ret < 0)
  {
    if (errno == EXDEV && copy_delete_cross_filesystem)
    {
      if (const auto copy_result = copy_file(old_path, new_path); !copy_result)
      {
        return jewels::unexpected(copy_result.error());
      }
      if (const auto unlink_result = unlink(old_path); !unlink_result)
      {
        return jewels::unexpected(unlink_result.error());
      }
      return {};
    }
    const auto error = make_error_code(errno);
    if (verbosity_ != ErrorVerbosity::off)
    {
      jewels::log_cerr_error("Failed to rename '{}' to '{}': {}", old_path, new_path, error.message());
    }
    return jewels::unexpected(error);
  }
  return {};
}

[[nodiscard]] jewels::expected<void, ErrorCode> Filesystem::unlink(std::string_view path) const
{
  const std::pmr::string path_str{path, memory_resource_};
  if (const auto ret = ::unlink(path_str.c_str()); ret < 0)
  {
    const auto error = make_error_code(errno);
    if (verbosity_ != ErrorVerbosity::off)
    {
      jewels::log_cerr_error("Failed to unlink '{}': {}", path, error.message());
    }
    return jewels::unexpected(error);
  }
  return {};
}

[[nodiscard]] jewels::expected<void, ErrorCode>
Filesystem::create_directory(std::string_view path, uint32_t perms) const
{
  const std::pmr::string path_str{path, memory_resource_};
  if (const auto ret = ::mkdir(path_str.c_str(), perms); ret < 0)
  {
    const auto error = make_error_code(errno);
    if (verbosity_ != ErrorVerbosity::off)
    {
      jewels::log_cerr_error("Failed to create directory '{}': {}", path, error.message());
    }
    return jewels::unexpected(error);
  }
  return {};
}

[[nodiscard]] jewels::expected<void, ErrorCode>
Filesystem::create_directories(std::string_view path, uint32_t perms) const
{
  std::pmr::list<Path> create_paths{memory_resource_};
  filesystem::Path dir_path{path, memory_resource_};
  while (!dir_path.empty() && dir_path != "/")
  {
    if (!dir_path.has_filename())
    {
      dir_path = dir_path.parent_path();
      continue;
    }
    const auto exists_result = exists(dir_path);
    if (!exists_result)
    {
      if (verbosity_ != ErrorVerbosity::off)
      {
        jewels::log_cerr_error("Failed to create directories '{}': {}", path, exists_result.error().message());
      }
      return jewels::unexpected(exists_result.error());
    }
    if (exists_result.value())
    {
      break;
    }
    create_paths.emplace_front(dir_path);
    dir_path = dir_path.parent_path();
  }
  for (const auto& create_path : create_paths)
  {
    const std::pmr::string path_str{create_path.string_view(), memory_resource_};
    if (const auto ret = ::mkdir(path_str.c_str(), perms); ret < 0 && errno != EEXIST)
    {
      const auto error = make_error_code(errno);
      if (verbosity_ != ErrorVerbosity::off)
      {
        jewels::log_cerr_error("Failed to create directory '{}': {}", path_str, error.message());
      }
      return jewels::unexpected(error);
    }
  }
  return {};
}

[[nodiscard]] jewels::expected<void, ErrorCode>
Filesystem::create_symlink(std::string_view target_path, std::string_view link_path) const
{
  const std::pmr::string target_path_str{target_path, memory_resource_};
  const std::pmr::string link_path_str{link_path, memory_resource_};
  if (const auto ret = ::symlink(target_path_str.c_str(), link_path_str.c_str()); ret < 0)
  {
    const auto error = make_error_code(errno);
    if (verbosity_ != ErrorVerbosity::off)
    {
      jewels::log_cerr_error("Failed to create symbolic link '{}': {}", link_path, error.message());
    }
    return jewels::unexpected(error);
  }
  return {};
}

[[nodiscard]] jewels::expected<void, ErrorCode>
Filesystem::create_hardlink(std::string_view target_path, std::string_view link_path) const
{
  const std::pmr::string target_path_str{target_path, memory_resource_};
  const std::pmr::string link_path_str{link_path, memory_resource_};
  if (const auto ret = ::link(target_path_str.c_str(), link_path_str.c_str()); ret < 0)
  {
    const auto error = make_error_code(errno);
    if (verbosity_ != ErrorVerbosity::off)
    {
      jewels::log_cerr_error("Failed to create hard link '{}': {}", link_path, error.message());
    }
    return jewels::unexpected(error);
  }
  return {};
}

[[nodiscard]] jewels::expected<std::pmr::string, ErrorCode> Filesystem::read_symlink(std::string_view link_path) const
{
  struct stat statbuf{};
  const std::pmr::string link_path_str{link_path, memory_resource_};
  if (const auto ret = ::lstat(link_path_str.c_str(), &statbuf); ret < 0)
  {
    const auto error = make_error_code(errno);
    if (verbosity_ != ErrorVerbosity::off)
    {
      jewels::log_cerr_error("Failed to stat symbolic link '{}'", link_path, error.message());
    }
    return jewels::unexpected(error);
  }
  std::pmr::string target_str(
    static_cast<size_t>(statbuf.st_size == 0 ? PATH_MAX : statbuf.st_size), '\0', memory_resource_);
  const auto ret = ::readlink(link_path_str.c_str(), target_str.data(), target_str.size());
  if (ret < 0)
  {
    const auto error = make_error_code(errno);
    if (verbosity_ != ErrorVerbosity::off)
    {
      jewels::log_cerr_error("Failed to read symbolic link '{}'", link_path, error.message());
    }
    return jewels::unexpected(error);
  }
  if (static_cast<size_t>(ret) < target_str.size())
  {
    target_str.resize(static_cast<size_t>(ret));
  }
  return {std::move(target_str)};
}

[[nodiscard]] jewels::expected<bool, ErrorCode> Filesystem::exists(std::string_view path) const
{
  const auto stat_result = stat_file(path, memory_resource_, verbosity_);
  if (!stat_result)
  {
    if (stat_result.error() == make_error_code(ENOENT))
    {
      return false;
    }
    return jewels::unexpected(stat_result.error());
  }
  return true;
}

[[nodiscard]] jewels::expected<bool, ErrorCode> Filesystem::is_directory(std::string_view path) const
{
  const auto stat_result = stat_file(path, memory_resource_, verbosity_);
  if (!stat_result)
  {
    if (stat_result.error() == make_error_code(ENOENT))
    {
      return false;
    }
    return jewels::unexpected(stat_result.error());
  }
  return (stat_result->st_mode & static_cast<uint32_t>(S_IFMT)) == S_IFDIR;
}

[[nodiscard]] jewels::expected<bool, ErrorCode> Filesystem::is_regular_file(std::string_view path) const
{
  const auto stat_result = stat_file(path, memory_resource_, verbosity_);
  if (!stat_result)
  {
    if (stat_result.error() == make_error_code(ENOENT))
    {
      return false;
    }
    return jewels::unexpected(stat_result.error());
  }
  return (stat_result->st_mode & static_cast<uint32_t>(S_IFMT)) == S_IFREG;
}

[[nodiscard]] jewels::expected<std::pmr::vector<filesystem::Path>, ErrorCode>
Filesystem::read_directory(std::string_view path) const
{
  return read_directory(
    path,
    [](const auto& dirent)
    {
      const std::string_view d_name{&dirent.d_name[0]};
      return d_name != "." && d_name != "..";
    });
}

[[nodiscard]] jewels::expected<std::pmr::vector<filesystem::Path>, ErrorCode>
Filesystem::read_directories(std::string_view path, bool ignore_permission_denied) const
{
  return read_directories(
    path,
    [](const auto& dirent)
    {
      const std::string_view d_name{&dirent.d_name[0]};
      return d_name != "." && d_name != "..";
    },
    ignore_permission_denied);
}

[[nodiscard]] jewels::expected<Filesystem::SpaceInformation, ErrorCode>
Filesystem::get_space_information(std::string_view path) const
{
  struct statfs statbuf{};
  const std::pmr::string path_str{path, memory_resource_};
  if (const auto stat_rc = statfs(path_str.c_str(), &statbuf); stat_rc < 0)
  {
    const auto error = make_error_code(errno);
    if (verbosity_ != ErrorVerbosity::off)
    {
      jewels::log_cerr_error("Failed to get filesystem status for '{}': {}", path, error.message());
    }
    return jewels::unexpected(error);
  }
  SpaceInformation space_info{};
  space_info.capacity = static_cast<size_t>(statbuf.f_bsize) * static_cast<size_t>(statbuf.f_blocks);
  space_info.free = static_cast<size_t>(statbuf.f_bsize) * static_cast<size_t>(statbuf.f_bfree);
  space_info.available = static_cast<size_t>(statbuf.f_bsize) * static_cast<size_t>(statbuf.f_bavail);
  return space_info;
}

jewels::expected<void, ErrorCode> Filesystem::remove(std::string_view path) const
{
  const std::pmr::string path_str{path, memory_resource_};
  const auto result = ::remove(path_str.c_str());
  if (result != 0U)
  {
    const auto err_code = make_error_code(errno);
    if (verbosity_ != ErrorVerbosity::off)
    {
      jewels::log_cerr_error("Failed to remove '{}': {}", path, err_code.message());
    }
    return jewels::unexpected(err_code);
  }
  return {};
}

jewels::expected<size_t, ErrorCode> Filesystem::remove_all(std::string_view path) const
{
  size_t count{0UL};

  auto expected_directory = is_directory(path);
  if (!expected_directory)
  {
    return jewels::unexpected{expected_directory.error()};
  }

  if (!expected_directory.value())
  {
    // path is a file, delete it
    const auto expected_remove = remove(path);
    if (!expected_remove)
    {
      return jewels::unexpected{expected_remove.error()};
    }
    return {1UL};
  }

  std::pmr::list<Path> entries{memory_resource_};
  entries.emplace_back(path, memory_resource_);

  for (auto& entry : entries)
  {
    auto expected_read = read_directory(entry);
    if (!expected_read)
    {
      return jewels::unexpected{expected_read.error()};
    }

    for (auto& read_entry : expected_read.value())
    {
      expected_directory = is_directory(entry / read_entry);
      if (!expected_directory)
      {
        return jewels::unexpected{expected_directory.error()};
      }

      if (!expected_directory.value())
      {
        // read_entry is a file, delete it
        const auto expected_remove = remove(entry / read_entry);
        if (!expected_remove)
        {
          return jewels::unexpected{expected_remove.error()};
        }
        ++count;
        continue;
      }

      entries.emplace_back(entry / read_entry);
    }
  }

  // iterate in reverse to remove subdirectories before parent directories
  for (auto& entry : entries | std::views::reverse)
  {
    const auto expected_remove = remove(entry);
    if (!expected_remove)
    {
      return jewels::unexpected{expected_remove.error()};
    }
    ++count;
  }

  return count;
}

[[nodiscard]] jewels::expected<filesystem::Path, ErrorCode> Filesystem::search_path(std::string_view binary_name) const
{
  // Validate that binary_name is a simple filename (no path separators)
  if (binary_name.find('/') != std::string_view::npos)
  {
    if (verbosity_ != ErrorVerbosity::off)
    {
      jewels::log_cerr_error("binary_name must be a simple filename, not a path: '{}'", binary_name);
    }
    return jewels::unexpected(make_error_code(ENOENT));
  }

  // Get PATH environment variable
  const char* path_env = std::getenv("PATH"); // NOLINT(concurrency-mt-unsafe) Thread-unsafety documented in api
  if (path_env == nullptr || std::strlen(path_env) == 0U)
  {
    if (verbosity_ != ErrorVerbosity::off)
    {
      jewels::log_cerr_error("PATH environment variable is not set or empty");
    }
    return jewels::unexpected(make_error_code(ENOENT));
  }

  for (const auto& entry : std::string_view{path_env} | std::views::split(':'))
  {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic) Address-of-dereference needed: split_view's
    // inner iterator satisfies only forward_iterator, so string_view's contiguous_iterator constructor is unavailable
    const std::string_view entry_sv{&*entry.begin(), static_cast<size_t>(std::ranges::distance(entry))};
    if (entry_sv.empty())
    {
      continue;
    }

    const filesystem::Path candidate_path = filesystem::Path{entry_sv, memory_resource_} / binary_name;

    const auto stat_result = stat_file(candidate_path.string_view(), memory_resource_, verbosity_);
    if (!stat_result)
    {
      if (stat_result.error() != make_error_code(ENOENT))
      {
        return jewels::unexpected(stat_result.error());
      }
      // ENOENT is expected when the binary is not in this directory; other errors are already logged by stat_file
      continue;
    }

    constexpr auto executable_bits = S_IXUSR | S_IXGRP | S_IXOTH;
    if ((stat_result->st_mode & static_cast<uint32_t>(executable_bits)) != 0U)
    {
      return filesystem::Path{candidate_path.string_view(), memory_resource_};
    }
  }

  if (verbosity_ != ErrorVerbosity::off)
  {
    jewels::log_cerr_error("Binary '{}' not found in PATH", binary_name);
  }
  return jewels::unexpected(make_error_code(ENOENT));
}

[[nodiscard]] jewels::expected<void, ErrorCode> remove(const jewels::filesystem::Path& path)
{
  if (const auto result = ::remove(path.c_str()); result != 0U)
  {
    const auto err_code = make_error_code(errno);
    return jewels::unexpected(err_code);
  }
  return {};
}
} // namespace jewels::filesystem
