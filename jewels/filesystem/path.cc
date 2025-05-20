// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/filesystem/path.hh"

#include <algorithm>
#include <ostream>
#include <utility>

namespace jewels::filesystem
{

Path::Path(jewels::memory::MemoryResource memory_resource) noexcept
  : path_(memory_resource), memory_resource_(std::move(memory_resource))
{
}

Path::Path(std::string_view path, jewels::memory::MemoryResource memory_resource) noexcept
  : path_(path, memory_resource), memory_resource_(std::move(memory_resource))
{
}

/// Copy constructor
/// @param[in] other Source
Path::Path(const Path& other)
  : path_(other.path_, other.memory_resource_), memory_resource_(other.memory_resource_)
{
}

Path& Path::operator=(const Path& other)
{
  if (this != &other)
  {
    path_ = std::pmr::string(other.path_, other.memory_resource_);
    memory_resource_ = other.memory_resource_;
  }
  return *this;
}

[[nodiscard]] bool Path::empty() const noexcept
{
  return path_.empty();
}

[[nodiscard]] Path::operator std::string_view() const noexcept
{
  return path_;
}

[[nodiscard]] const char* Path::c_str() const noexcept
{
  return path_.c_str();
}

[[nodiscard]] const std::pmr::string& Path::string() const noexcept
{
  return path_;
}

[[nodiscard]] std::string_view Path::string_view() const noexcept
{
  return path_;
}

[[nodiscard]] bool Path::has_parent_path() const noexcept
{
  return !parent_path_view().empty();
}

[[nodiscard]] bool Path::has_filename() const noexcept
{
  return !filename_view().empty();
}

[[nodiscard]] bool Path::has_stem() const noexcept
{
  return !stem_view().empty();
}

[[nodiscard]] bool Path::has_extension() const noexcept
{
  return !extension_view().empty();
}

[[nodiscard]] Path Path::parent_path() const
{
  return {parent_path_view(), memory_resource_};
}

[[nodiscard]] Path Path::filename() const
{
  return {filename_view(), memory_resource_};
}

[[nodiscard]] Path Path::stem() const
{
  return {stem_view(), memory_resource_};
}

[[nodiscard]] Path Path::extension() const
{
  return {extension_view(), memory_resource_};
}

void Path::clear() noexcept
{
  path_.clear();
}

Path& Path::remove_filename() noexcept
{
  const auto last_slash_pos = path_.find_last_of('/');
  if (last_slash_pos == std::string::npos)
  {
    path_.clear();
    return *this;
  }
  path_.resize(last_slash_pos + 1U);
  return *this;
}

Path& Path::replace_filename(std::string_view path)
{
  remove_filename();
  return operator/=(path);
}

Path& Path::replace_extension(std::string_view extension)
{
  if (has_extension())
  {
    const auto last_dot_pos = path_.find_last_of('.');
    path_.resize(last_dot_pos);
  }
  if (!extension.empty() && extension.front() != '.')
  {
    path_.append(".");
  }
  path_.append(extension);
  return *this;
}

Path& Path::operator/=(std::string_view path)
{
  if (!path.empty() && path.front() == '/')
  {
    path_.assign(path);
    return *this;
  }
  if (!path_.empty() && path_.back() != '/')
  {
    path_.append("/");
  }
  path_.append(path);
  return *this;
}

Path& Path::operator+=(std::string_view path)
{
  path_.append(path);
  return *this;
}

[[nodiscard]] std::string_view Path::parent_path_view() const noexcept
{
  if (std::ranges::all_of(path_, [](const char value) { return value == '/'; }))
  {
    return path_;
  }
  const auto last_slash_pos = path_.find_last_of('/');
  if (last_slash_pos == std::string::npos)
  {
    return {};
  }
  auto first_slash_pos = last_slash_pos;
  while ((first_slash_pos != 0U) && (path_.at(first_slash_pos - 1U) == '/'))
  {
    --first_slash_pos;
  }
  if (first_slash_pos == 0U)
  {
    return {path_.data(), last_slash_pos + 1U};
  }
  return {path_.data(), first_slash_pos};
}

[[nodiscard]] std::string_view Path::filename_view() const noexcept
{
  const auto last_slash_pos = path_.find_last_of('/');
  if (last_slash_pos == std::string::npos)
  {
    return path_;
  }
  if (last_slash_pos + 1U == path_.size())
  {
    return {};
  }
  return {&path_.at(last_slash_pos + 1U)};
}

[[nodiscard]] std::string_view Path::stem_view() const noexcept
{
  const auto filename = filename_view();
  if (!has_extension())
  {
    return filename;
  }
  const auto last_dot_pos = filename.find_last_of('.');
  return filename.substr(0, last_dot_pos);
}

[[nodiscard]] std::string_view Path::extension_view() const noexcept
{
  const auto filename = filename_view();
  if (filename == "." || filename == "..")
  {
    return {};
  }
  const auto last_dot_pos = filename.find_last_of('.');
  if (last_dot_pos == std::string_view::npos)
  {
    return {};
  }
  if (last_dot_pos == 0U)
  {
    return {};
  }
  return filename.substr(last_dot_pos);
}

std::ostream& operator<<(std::ostream& ostream, const Path& path)
{
  ostream << static_cast<std::string_view>(path);
  return ostream;
}

} // namespace jewels::filesystem
