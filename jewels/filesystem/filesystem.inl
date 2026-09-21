#pragma once

// IWYU pragma: private, include "jewels/filesystem/filesystem.hh"

// IWYU pragma: no_include <bits/syscall-64.h>

#include "jewels/filesystem/filesystem.hh"

#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <dirent.h>
#include <list>
#include <memory_resource>
#include <string>
#include <string_view>
#include <sys/syscall.h> // IWYU pragma: keep
#include <sys/types.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace jewels::filesystem
{

template <typename FilterFunctionType>
[[nodiscard]] jewels::expected<std::pmr::vector<filesystem::Path>, ErrorCode>
Filesystem::read_directory(std::string_view path, FilterFunctionType&& filter_fn) const
{
  const auto& filter_fn_ref = std::forward<FilterFunctionType>(filter_fn);
  return read_directory(path, filter_fn_ref);
}

template <typename FilterFunctionType>
[[nodiscard]] jewels::expected<std::pmr::vector<filesystem::Path>, ErrorCode>
Filesystem::read_directory(std::string_view path, const FilterFunctionType& filter_fn) const
{
  std::pmr::vector<filesystem::Path> entries(memory_resource_);
  if (const auto read_result = read_directory_impl(path, "", filter_fn, false, entries); !read_result)
  {
    return jewels::unexpected(read_result.error());
  }
  std::ranges::sort(entries);
  return {std::move(entries)};
}

template <typename FilterFunctionType>
[[nodiscard]] jewels::expected<std::pmr::vector<filesystem::Path>, ErrorCode>
Filesystem::read_directories(std::string_view path, FilterFunctionType&& filter_fn, bool ignore_permission_denied) const
{
  const auto& filter_fn_ref = std::forward<FilterFunctionType>(filter_fn);
  return read_directories(path, filter_fn_ref, ignore_permission_denied);
}

template <typename FilterFunctionType>
[[nodiscard]] jewels::expected<std::pmr::vector<filesystem::Path>, ErrorCode> Filesystem::read_directories(
  std::string_view path, const FilterFunctionType& filter_fn, bool ignore_permission_denied) const
{
  std::pmr::list<DirectoryToRead> directories_to_read{memory_resource_};
  directories_to_read.emplace_back(filesystem::Path{path, memory_resource_}, filesystem::Path{memory_resource_});
  std::pmr::vector<filesystem::Path> entries(memory_resource_);
  while (!directories_to_read.empty())
  {
    const auto dir_path = std::move(directories_to_read.front().directory_path);
    const auto parent_path = std::move(directories_to_read.front().parent_path);
    directories_to_read.pop_front();
    std::pmr::vector<filesystem::Path> subdir_entries(memory_resource_);
    if (const auto read_result = read_directory_impl(
          dir_path,
          "",
          [](const auto& dent)
          {
            const std::string_view d_name{&dent.d_name[0]};
            return dent.d_type == DT_DIR && d_name != "." && d_name != "..";
          },
          ignore_permission_denied,
          subdir_entries);
        !read_result)
    {
      return jewels::unexpected(read_result.error());
    }
    for (auto& subdir_path : subdir_entries)
    {
      directories_to_read.emplace_back(dir_path / subdir_path, parent_path / subdir_path);
    }
    if (const auto read_result =
          read_directory_impl(dir_path, parent_path, filter_fn, ignore_permission_denied, entries);
        !read_result)
    {
      return jewels::unexpected(read_result.error());
    }
  }
  std::ranges::sort(entries);
  return {std::move(entries)};
}

template <typename FilterFunctionType>
[[nodiscard]] jewels::expected<void, ErrorCode> Filesystem::read_directory_impl(
  std::string_view path,
  std::string_view parent_path,
  const FilterFunctionType& filter_fn,
  bool ignore_permission_denied,
  std::pmr::vector<filesystem::Path>& entries) const
{
  // Not using readdir(3) which uses malloc to allocate buffers
  const auto open_result = open(path);
  if (!open_result)
  {
    if (ignore_permission_denied && open_result.error() == make_error_code(EACCES))
    {
      return {};
    }
    return jewels::unexpected(open_result.error());
  }
  constexpr size_t buffer_size = 8192U;
  const filesystem::Path parent_dir{parent_path, memory_resource_};
  std::pmr::vector<std::byte> buffer(buffer_size, memory_resource_);
  while (true)
  {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) needed for syscall interface which uses varargs
    const auto num_read = syscall(SYS_getdents64, **open_result, buffer.data(), buffer.size());
    if (num_read < 0)
    {
      const auto err_code = make_error_code(errno);
      if (verbosity_ != ErrorVerbosity::off)
      {
        jewels::log_cerr_error("Failed to read entries from '{}': {}", path, err_code.message());
      }
      return jewels::unexpected(err_code);
    }
    if (num_read == 0)
    {
      break;
    }
    for (ssize_t offset = 0; offset < num_read;)
    {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) necessary to interpret the buffer as dirent64
      const LinuxDirent64* dirent_ptr = reinterpret_cast<LinuxDirent64*>(&buffer.at(static_cast<size_t>(offset)));
      if (filter_fn(*dirent_ptr))
      {
        entries.push_back(parent_dir / &dirent_ptr->d_name[0U]);
      }
      offset += dirent_ptr->d_reclen;
    }
  }
  return {};
}

} // namespace jewels::filesystem
