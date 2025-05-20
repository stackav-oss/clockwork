// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/offboard/log_uri.hh"

#include <fmt10/base.h>

#include <iterator>

namespace clockwork_logging::offboard
{

namespace
{

/// Regular file URI prefix
constexpr auto file_prefix = std::string_view{"file:"};

/// S3 URI prefix
constexpr auto s3_prefix = std::string_view{"s3:"};
constexpr auto s3_full_prefix = std::string_view{"s3://"};

} // namespace

LogUri::LogUri(
  LogUriScheme scheme,
  std::pmr::string scheme_prefix,
  std::pmr::string host,
  jewels::filesystem::Path path,
  jewels::memory::MemoryResource memory_resource)
  : scheme_(scheme),
    scheme_prefix_(std::move(scheme_prefix)),
    host_(std::move(host)),
    path_(std::move(path)),
    memory_resource_(std::move(memory_resource))
{
}

LogUri::LogUri(const LogUri& other)
  : scheme_(other.scheme_),
    scheme_prefix_(other.scheme_prefix_, other.memory_resource_),
    host_(other.host_, other.memory_resource_),
    path_(other.path_),
    memory_resource_(other.memory_resource_)
{
}

LogUri& LogUri::operator=(const LogUri& other)
{
  if (this != &other)
  {
    scheme_ = other.scheme_;
    scheme_prefix_ = std::pmr::string{other.scheme_prefix_, other.memory_resource_};
    host_ = std::pmr::string{other.host_, other.memory_resource_};
    path_ = other.path_;
    memory_resource_ = other.memory_resource_;
  }
  return *this;
}

[[nodiscard]] jewels::expected<LogUri, jewels::MonoError>
LogUri::try_make(std::string_view uri_str, jewels::memory::MemoryResource memory_resource)
{
  LogUriScheme scheme{};
  std::pmr::string scheme_prefix;
  std::pmr::string host;
  jewels::filesystem::Path path{memory_resource};
  if (uri_str.starts_with(file_prefix))
  {
    scheme = LogUriScheme::file;
    scheme_prefix = std::pmr::string{file_prefix, memory_resource};
    path = jewels::filesystem::Path{uri_str.substr(file_prefix.size()), memory_resource};
    if (path.string_view().empty())
    {
      return jewels::unexpected{jewels::MonoError{}};
    }
  }
  else if (uri_str.starts_with(s3_prefix))
  {
    if (!uri_str.starts_with(s3_full_prefix))
    {
      return jewels::unexpected{jewels::MonoError{}};
    }
    scheme = LogUriScheme::s3;
    scheme_prefix = std::pmr::string{s3_full_prefix, memory_resource};
    auto remainder = uri_str.substr(s3_full_prefix.size());
    const auto slash_pos = remainder.find('/');
    if (slash_pos == 0U || slash_pos == std::string_view::npos)
    {
      return jewels::unexpected{jewels::MonoError{}};
    }
    host = std::pmr::string{remainder.substr(0U, slash_pos), memory_resource};
    path = jewels::filesystem::Path{remainder.substr(slash_pos), memory_resource};
    if (!path.string_view().starts_with("/"))
    {
      return jewels::unexpected{jewels::MonoError{}};
    }
  }
  else
  {
    scheme = LogUriScheme::file;
    path = jewels::filesystem::Path{uri_str, memory_resource};
    if (path.string_view().empty())
    {
      return jewels::unexpected{jewels::MonoError{}};
    }
  }
  return LogUri{scheme, std::move(scheme_prefix), std::move(host), std::move(path), memory_resource};
}

[[nodiscard]] std::pmr::string LogUri::string() const
{
  std::pmr::string uri_str(memory_resource_);
  uri_str.reserve(scheme_prefix_.size() + host_.size() + path_.string_view().size());
  fmt::format_to(std::back_inserter(uri_str), "{}{}{}", scheme_prefix_, host_, path_.string_view());
  return uri_str;
}

[[nodiscard]] bool LogUri::has_filename() const noexcept
{
  return path_.has_filename();
}

[[nodiscard]] bool LogUri::has_stem() const noexcept
{
  return path_.has_stem();
}

[[nodiscard]] bool LogUri::has_extension() const noexcept
{
  return path_.has_extension();
}

[[nodiscard]] bool LogUri::has_host() const noexcept
{
  return !host_.empty();
}

[[nodiscard]] LogUriScheme LogUri::scheme() const noexcept
{
  return scheme_;
}

[[nodiscard]] std::string_view LogUri::host() const noexcept
{
  return host_;
}

[[nodiscard]] std::string_view LogUri::path() const noexcept
{
  return path_.string_view();
}

[[nodiscard]] LogUri LogUri::parent_uri() const
{
  return LogUri{
    scheme_,
    std::pmr::string{scheme_prefix_, memory_resource_},
    std::pmr::string{host_, memory_resource_},
    path_.parent_path(),
    memory_resource_};
}

[[nodiscard]] std::string_view LogUri::filename() const noexcept
{
  return path_.filename_view();
}

[[nodiscard]] std::string_view LogUri::stem() const noexcept
{
  return path_.stem_view();
}

[[nodiscard]] std::string_view LogUri::extension() const noexcept
{
  return path_.extension_view();
}

LogUri& LogUri::remove_filename() noexcept
{
  path_.remove_filename();
  return *this;
}

LogUri& LogUri::replace_filename(std::string_view path)
{
  path_.replace_filename(path);
  return *this;
}

LogUri& LogUri::replace_extension(std::string_view extension)
{
  path_.replace_extension(extension);
  return *this;
}

LogUri& LogUri::operator/=(std::string_view path)
{
  path_ /= path;
  return *this;
}

LogUri& LogUri::operator+=(std::string_view path)
{
  path_ += path;
  return *this;
}

[[nodiscard]] LogUri LogUri::apply_relative_path(std::string_view relative_path) const
{
  auto result_uri = *this;
  while (!result_uri.path().empty() && result_uri.path() != "/" && result_uri.path().back() == '/')
  {
    result_uri = result_uri.parent_uri();
  }
  while (!relative_path.empty())
  {
    if (relative_path.starts_with("./"))
    {
      relative_path.remove_prefix(2U);
      continue;
    }
    if (relative_path.starts_with("../"))
    {
      if (result_uri.path().empty())
      {
        result_uri /= relative_path;
        break;
      }
      result_uri = result_uri.parent_uri();
      relative_path.remove_prefix(3U);
      continue;
    }
    if (relative_path == ".")
    {
      break;
    }
    if (relative_path == "..")
    {
      if (result_uri.path().empty())
      {
        result_uri /= relative_path;
        break;
      }
      result_uri = result_uri.parent_uri();
      break;
    }
    result_uri /= relative_path;
    break;
  }
  if (result_uri.path().empty())
  {
    result_uri.path_ = jewels::filesystem::Path{".", memory_resource_};
  }
  return result_uri;
}

std::ostream& operator<<(std::ostream& ostream, LogUriScheme value)
{
  ostream << wise_enum::to_string(value);
  return ostream;
}

std::ostream& operator<<(std::ostream& ostream, const LogUri& log_uri)
{
  ostream << log_uri.string();
  return ostream;
}

} // namespace clockwork_logging::offboard
