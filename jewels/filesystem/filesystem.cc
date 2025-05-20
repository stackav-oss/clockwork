// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/filesystem/filesystem.hh"

#include "jewels/filesystem/path.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/std/expected.hh"

#include <array>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <list>
#include <memory_resource>
#include <ranges>
#include <string>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/statfs.h>
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
Filesystem::open(std::string_view file_path, int32_t mode_flags, uint32_t perms)
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

[[nodiscard]] jewels::expected<size_t, ErrorCode> Filesystem::get_size(const FileDescriptor& file_desc)
{
  const auto stat_result = stat_file(file_desc, verbosity_);
  if (!stat_result)
  {
    return jewels::unexpected(stat_result.error());
  }
  return static_cast<size_t>(stat_result->st_size);
}

[[nodiscard]] jewels::expected<size_t, ErrorCode> Filesystem::get_size(std::string_view file_path)
{
  const auto stat_result = stat_file(file_path, memory_resource_, verbosity_);
  if (!stat_result)
  {
    return jewels::unexpected(stat_result.error());
  }
  return static_cast<size_t>(stat_result->st_size);
}

[[nodiscard]] jewels::expected<void, ErrorCode> Filesystem::touch(std::string_view path)
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
  auto expected_fd = open(file_path, O_WRONLY | O_CREAT);
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

[[nodiscard]] jewels::expected<filesystem::Path, ErrorCode> Filesystem::create_temporary_directory()
{
  const char* session_dir = std::getenv("XDG_SESSION_DIR"); // NOLINT(concurrency-mt-unsafe)
  if (session_dir == nullptr || std::strlen(session_dir) == 0)
  {
    session_dir = "/tmp"; // Fallback to /tmp if XDG_SESSION_DIR is not set
  }
  std::pmr::string template_path = std::pmr::string(session_dir, memory_resource_);
  constexpr auto only_me = 0700;
  auto possible_create_directories = create_directories(template_path, only_me);

  possible_create_directories.or_else([](const auto& error)
                                      { return jewels::expected<void, ErrorCode>(jewels::unexpected(error)); });

  template_path += "/XXXXXX";
  std::pmr::vector<char> template_path_chars(template_path.begin(), template_path.end(), memory_resource_);
  template_path_chars.push_back('\0');
  if (auto* const ret = ::mkdtemp(template_path_chars.data()); ret == nullptr)
  {
    const auto error = make_error_code(errno);
    if (verbosity_ != ErrorVerbosity::off)
    {
      jewels::log_cerr_error("Failed to create temporary directory '{}': {}", template_path, error.message());
    }
    return jewels::unexpected(error);
  }
  return filesystem::Path{template_path_chars.data(), memory_resource_};
}

[[nodiscard]] jewels::expected<jewels::time::SyncTime, ErrorCode>
Filesystem::get_last_write_time(const FileDescriptor& file_desc)
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
Filesystem::get_last_write_time(std::string_view file_path)
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
Filesystem::set_last_write_time(const FileDescriptor& file_desc, jewels::time::SyncTime time_to_set)
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
Filesystem::set_last_write_time(std::string_view file_path, jewels::time::SyncTime time_to_set)
{
  const auto open_result = open(file_path, O_RDWR);
  if (!open_result)
  {
    return jewels::unexpected(open_result.error());
  }
  return set_last_write_time(open_result.value(), time_to_set);
}

[[nodiscard]] jewels::expected<size_t, ErrorCode>
Filesystem::read(const FileDescriptor& file_desc, std::span<std::byte> data)
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
Filesystem::read(const FileDescriptor& file_desc, size_t offset, std::span<std::byte> data)
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
Filesystem::write(const FileDescriptor& file_desc, std::span<const std::byte> data)
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
Filesystem::write(const FileDescriptor& file_desc, size_t offset, std::span<const std::byte> data)
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

[[nodiscard]] jewels::expected<size_t, ErrorCode> Filesystem::get_position(const FileDescriptor& file_desc)
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

[[nodiscard]] jewels::expected<void, ErrorCode> Filesystem::set_position(const FileDescriptor& file_desc, size_t offset)
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
Filesystem::copy_file(std::string_view source_path, std::string_view destination_path)
{
  const std::pmr::string source_path_str{source_path, memory_resource_};
  const std::pmr::string destination_path_str{destination_path, memory_resource_};
  const auto possible_open_result = open(source_path_str, O_RDWR);
  if (!possible_open_result)
  {
    return jewels::unexpected(possible_open_result.error());
  }
  const auto& open_result = *possible_open_result;
  struct stat statbuf{};
  if (fstat(*open_result, &statbuf) == -1)
  {
    return jewels::unexpected(make_error_code(errno));
  }
  // Create new or truncate existing at destination
  auto possible_create_result = open(destination_path_str, O_WRONLY | O_CREAT | O_TRUNC);
  if (!possible_create_result)
  {
    return jewels::unexpected(possible_create_result.error());
  }
  auto& create_result = *possible_create_result;
  off_t copied = 0;
  while (copied < statbuf.st_size)
  {
    const ssize_t written = ::sendfile(*create_result, *open_result, &copied, SSIZE_MAX);
    copied += written;
    if (written == -1)
    {
      auto error = make_error_code(errno);
      return jewels::unexpected(error);
    }
  }
  return {};
}

[[nodiscard]] jewels::expected<void, ErrorCode>
Filesystem::rename(std::string_view old_path, std::string_view new_path, bool copy_delete_cross_filesystem)
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

[[nodiscard]] jewels::expected<void, ErrorCode> Filesystem::unlink(std::string_view path)
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

[[nodiscard]] jewels::expected<void, ErrorCode> Filesystem::create_directory(std::string_view path, uint32_t perms)
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

[[nodiscard]] jewels::expected<void, ErrorCode> Filesystem::create_directories(std::string_view path, uint32_t perms)
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
Filesystem::create_symlink(std::string_view target_path, std::string_view link_path)
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

[[nodiscard]] jewels::expected<std::pmr::string, ErrorCode> Filesystem::read_symlink(std::string_view link_path)
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

[[nodiscard]] jewels::expected<bool, ErrorCode> Filesystem::exists(std::string_view path)
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

[[nodiscard]] jewels::expected<bool, ErrorCode> Filesystem::is_directory(std::string_view path)
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

[[nodiscard]] jewels::expected<bool, ErrorCode> Filesystem::is_regular_file(std::string_view path)
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
Filesystem::read_directory(std::string_view path)
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
Filesystem::read_directories(std::string_view path, bool ignore_permission_denied)
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
Filesystem::get_space_information(std::string_view path)
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

jewels::expected<void, ErrorCode> Filesystem::remove(std::string_view path)
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

jewels::expected<size_t, ErrorCode> Filesystem::remove_all(std::string_view path)
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
} // namespace jewels::filesystem
