// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"

#include <wise_enum.h>

#include <compare>
#include <cstdint>
#include <memory_resource>
#include <ostream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace clockwork_logging::offboard
{

/// Log URI scheme
WISE_ENUM_CLASS((LogUriScheme, uint8_t), file, s3)

/// Log URI class based on jewels::filesystem::Path
///
/// Log URIs implement a subset of RFC3986 to represent log file paths. The implementation
/// takes liberties to allow strings without a scheme:// prefix to represent files on the
/// local filesystem rather than requiring users to always use the file:// prefix.
///
/// The basic format of a log URI is <scheme>:[//<host>]<path>
///
/// Limitations:
///   - The supported schemes are 'file' and 's3'.
///   - The path part must contain a leading '/'.
///   - Log URIs don't understand queries or fragments and will treat them as part of the path.
///   - File URIs don't understand hosts and treat them as part of the path
///   - Log URIS don't understand the  userinfo and port portions of the authority field and will
///     treat them as part of the host.
///   - The host field is required for S3 URIs and contains the name of the bucket where the path is stored.
class LogUri
{
  /// Private constructor, use try_make to create an instance
  /// @param[in] scheme Scheme type
  /// @param[in] scheme_prefix Scheme prefix
  /// @param[in] host Host part
  /// @param[in] path Path part
  /// @param[in] memory_resource Memory resource
  LogUri(
    LogUriScheme scheme,
    std::pmr::string scheme_prefix,
    std::pmr::string host,
    jewels::filesystem::Path path,
    jewels::memory::MemoryResource memory_resource);

public:
  /// Construct an empty URI
  /// @param[in] memory_resource Memory resource
  explicit LogUri(jewels::memory::MemoryResource memory_resource);

  ~LogUri() noexcept = default;

  /// Copy constructor
  /// @param[in] other Source
  LogUri(const LogUri& other);

  /// Copy assignment
  /// @param[in] other Source
  /// Return reference to this instance
  LogUri& operator=(const LogUri& other);

  LogUri(LogUri&&) noexcept = default;
  LogUri& operator=(LogUri&&) noexcept = default;

  /// Construct a log URI from a string.
  /// @param[in] uri_str URI string
  /// @param[in] memory_resource Memory resource
  /// @return Log URI instance or MonoError if the URI is invalid
  [[nodiscard]] static jewels::expected<LogUri, jewels::MonoError>
  try_make(std::string_view uri_str, jewels::memory::MemoryResource memory_resource);

  /// Get the URI string
  /// @return URI string
  [[nodiscard]] std::pmr::string string() const;

  /// Check if the filename is present
  /// @return True iff the filename is not empty
  [[nodiscard]] bool has_filename() const noexcept;

  /// Check if the stem is present
  /// @return True iff the filename without the final extension is not empty
  [[nodiscard]] bool has_stem() const noexcept;

  /// Check if the extension is present
  /// @return True iff the filename has an extension
  [[nodiscard]] bool has_extension() const noexcept;

  /// Check if the host is present
  /// @return True iff the URI has a host part
  [[nodiscard]] bool has_host() const noexcept;

  /// Get the URI scheme
  /// @return URI scheme
  [[nodiscard]] LogUriScheme scheme() const noexcept;

  /// Get the URI host part
  /// @return URI host part
  [[nodiscard]] std::string_view host() const noexcept;

  /// Get the URI path part
  /// @return URI path part
  [[nodiscard]] std::string_view path() const noexcept;

  /// Return the parent URI
  /// @return URI for the parent directory
  [[nodiscard]] LogUri parent_uri() const;

  /// Return the filename
  /// @return File name path component
  [[nodiscard]] std::string_view filename() const noexcept;

  /// Return the stem path
  /// @return File name without final extension
  [[nodiscard]] std::string_view stem() const noexcept;

  /// Return the extension to the path component
  /// @return File extension
  [[nodiscard]] std::string_view extension() const noexcept;

  /// Remove the filename path component
  /// @return Reference to this instance
  LogUri& remove_filename() noexcept;

  /// Replace the filename path component with another path
  /// @param[in] path New path
  /// @return Reference to this instance
  LogUri& replace_filename(std::string_view path);

  /// Replace the filename extension
  /// @param[in] extension New extension
  /// @return Reference to this instance
  LogUri& replace_extension(std::string_view extension);

  /// Append a path with an added slash separator
  /// @param[in] path Path to append
  /// @return Reference to this instance
  LogUri& operator/=(std::string_view path);

  /// Append path without an added slash
  /// @param[in] path Path to append
  /// @return Reference to this instance
  LogUri& operator+=(std::string_view path);

  /// Concatenate two paths with a slash separator
  /// @param[in] lhs Left hand side
  /// @param[in] rhs Right side
  /// @return Concatenated paths
  [[nodiscard]] friend LogUri operator/(const LogUri& lhs, std::string_view rhs)
  {
    auto result_path = lhs.path_;
    result_path /= rhs;
    return LogUri{
      lhs.scheme_,
      std::pmr::string{lhs.scheme_prefix_, lhs.memory_resource_},
      std::pmr::string{lhs.host_, lhs.memory_resource_},
      std::move(result_path),
      lhs.memory_resource_};
  }

  /// Concatenate two paths without a slash separator
  /// @param[in] lhs Left hand side
  /// @param[in] rhs Right side
  /// @return Concatenated paths
  [[nodiscard]] friend LogUri operator+(const LogUri& lhs, std::string_view rhs)
  {
    auto result_path = lhs.path_;
    result_path += rhs;
    return LogUri{
      lhs.scheme_,
      std::pmr::string{lhs.scheme_prefix_, lhs.memory_resource_},
      std::pmr::string{lhs.host_, lhs.memory_resource_},
      std::move(result_path),
      lhs.memory_resource_};
  }

  /// Get the absolute path from a path relative to this URI
  /// @param[in] relative_path Relative path with one or more leading "../" substrings
  /// @return Absolute path resulting from applying the relative path
  [[nodiscard]] LogUri apply_relative_path(std::string_view relative_path) const;

  /// Comparison operator with null terminated array of char
  /// @param[in] lhs Left hand side operand
  /// @param[in] rhs Right hand side operand
  /// @return True iff lhs == rhs
  [[nodiscard]] friend bool operator==(const LogUri& lhs, const char* rhs) noexcept
  {
    const auto make_result = try_make(rhs, lhs.memory_resource_);
    return make_result == lhs;
  }

  /// Comparison operator with null terminated array of char
  /// @param[in] lhs Left hand side operand
  /// @param[in] rhs Right hand side operand
  /// @return True iff lhs != rhs
  [[nodiscard]] friend bool operator!=(const LogUri& lhs, const char* rhs) noexcept
  {
    return !(lhs == rhs);
  }

  /// Comparison operator
  /// @param[in] lhs Left hand side operand
  /// @param[in] rhs Right hand side operand
  /// @return True iff lhs == rhs
  [[nodiscard]] friend bool operator==(const LogUri& lhs, const LogUri& rhs) noexcept
  {
    return std::tie(lhs.scheme_, lhs.host_, lhs.path_) == std::tie(rhs.scheme_, rhs.host_, rhs.path_);
  }

  /// Comparison operator
  /// @param[in] lhs Left hand side operand
  /// @param[in] rhs Right hand side operand
  /// @return True iff lhs < rhs
  [[nodiscard]] friend bool operator<(const LogUri& lhs, const LogUri& rhs) noexcept
  {
    return std::tie(lhs.scheme_, lhs.host_, lhs.path_) < std::tie(rhs.scheme_, rhs.host_, rhs.path_);
  }

  /// Comparison operator
  /// @param[in] lhs Left hand side operand
  /// @param[in] rhs Right hand side operand
  /// @return True iff lhs > rhs
  [[nodiscard]] friend bool operator>(const LogUri& lhs, const LogUri& rhs) noexcept
  {
    return std::tie(lhs.scheme_, lhs.host_, lhs.path_) > std::tie(rhs.scheme_, rhs.host_, rhs.path_);
  }

  /// Comparison operator
  /// @param[in] lhs Left hand side operand
  /// @param[in] rhs Right hand side operand
  /// @return True iff lhs != rhs
  [[nodiscard]] friend bool operator!=(const LogUri& lhs, const LogUri& rhs) noexcept
  {
    return !(lhs == rhs);
  }

  /// Comparison operator
  /// @param[in] lhs Left hand side operand
  /// @param[in] rhs Right hand side operand
  /// @return True iff lhs <= rhs
  [[nodiscard]] friend bool operator<=(const LogUri& lhs, const LogUri& rhs) noexcept
  {
    return !(lhs > rhs);
  }

  /// Comparison operator
  /// @param[in] lhs Left hand side operand
  /// @param[in] rhs Right hand side operand
  /// @return True iff lhs >= rhs
  [[nodiscard]] friend bool operator>=(const LogUri& lhs, const LogUri& rhs) noexcept
  {
    return !(lhs < rhs);
  }

  /// Test whether a path is an absolute path
  ///
  /// Absolute paths start with '/', 'file:', or 's3:'.
  ///
  /// @param[in] path_str Path string
  /// @return True if the path is absolute, otherwise false
  [[nodiscard]] static bool is_absolute_path(std::string_view path_str);

private:
  /// URI scheme
  LogUriScheme scheme_;

  /// URI scheme prefix (includs // if host is present)
  std::pmr::string scheme_prefix_;

  /// Host part, empty if not present
  std::pmr::string host_;

  /// Path part
  jewels::filesystem::Path path_;

  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;
};

/// Output stream insertion operator for LogUriScheme
/// @param[in] ostream Output stream
/// @param[in] value LogUriScheme value
/// @return Output stream reference
std::ostream& operator<<(std::ostream& ostream, LogUriScheme value);

/// Output stream insertion operator for LogUri
/// @param[in] ostream Output stream
/// @param[in] log_uri Log URI
/// @return Output stream reference
std::ostream& operator<<(std::ostream& ostream, const LogUri& log_uri);

} // namespace clockwork_logging::offboard
