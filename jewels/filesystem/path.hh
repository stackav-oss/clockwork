// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/memory/memory_resource.hh"

#include <compare>
#include <iosfwd>
#include <memory_resource>
#include <string>
#include <string_view>

namespace jewels::filesystem
{

/// Allocator aware filesystem path
class Path
{
public:
  /// Constructor
  /// @param[in] memory_resource
  explicit Path(jewels::memory::MemoryResource memory_resource) noexcept;

  /// Constructor
  /// @param[in] path Path string
  /// @param[in] memory_resource
  Path(std::string_view path, jewels::memory::MemoryResource memory_resource) noexcept;

  ~Path() noexcept = default;

  /// Copy constructor
  /// @param[in] other Source
  Path(const Path& other);

  /// Copy assignment
  /// @param[in] other Source
  /// Return reference to this instance
  Path& operator=(const Path& other);

  Path(Path&&) noexcept = default;
  Path& operator=(Path&&) noexcept = default;

  /// Test whether the path is empty
  /// @return True if the path is empty
  [[nodiscard]] bool empty() const noexcept;

  /// Implicit conversion to std::string_view
  /// @return Path string view
  [[nodiscard]] operator std::string_view() const noexcept; // NOLINT(google-explicit-constructor) deliberately implicit

  /// Access the path as a 'C' string
  /// @return String pointer
  [[nodiscard]] const char* c_str() const noexcept;

  /// Access the path string
  /// @return Path string
  [[nodiscard]] std::pmr::string string() const noexcept;

  /// Get a a string view for the path string
  /// @return Path string view
  [[nodiscard]] std::string_view string_view() const noexcept;

  /// Check if the parent path is present
  /// @return True iff the parent path is not empty
  [[nodiscard]] bool has_parent_path() const noexcept;

  /// Check if the filename is present
  /// @return True iff the filename is not empty
  [[nodiscard]] bool has_filename() const noexcept;

  /// Check if the stem is present
  /// @return True iff the filename without the final extension is not empty
  [[nodiscard]] bool has_stem() const noexcept;

  /// Check if the extension is present
  /// @return True iff the filename has an extension
  [[nodiscard]] bool has_extension() const noexcept;

  /// Check if the path is absolute
  /// @return True if the path is absolute
  [[nodiscard]] bool is_absolute() const noexcept;

  /// Return the parent path
  /// @return Path to the parent directory
  [[nodiscard]] Path parent_path() const;

  /// Return the filename
  /// @return File name path component
  [[nodiscard]] Path filename() const;

  /// Return the stem path
  /// @return File name without final extension
  [[nodiscard]] Path stem() const;

  /// Return the extension to the path component
  /// @return File extension
  [[nodiscard]] Path extension() const;

  /// Clear the path string
  void clear() noexcept;

  /// Remove the filename path component
  /// @return Reference to this instance
  Path& remove_filename() noexcept;

  /// Replace the filename path component with another path
  /// @param[in] path New path
  /// @return Reference to this instance
  Path& replace_filename(std::string_view path);

  /// Replace the filename extension
  /// @param[in] extension New extension
  /// @return Reference to this instance
  Path& replace_extension(std::string_view extension);

  /// Append a path with an added slash separator
  /// @param[in] path Path to append
  /// @return Reference to this instance
  Path& operator/=(std::string_view path);

  /// Append path without an added slash
  /// @param[in] path Path to append
  /// @return Reference to this instance
  Path& operator+=(std::string_view path);

  /// Concatenate two paths with a slash separator
  /// @param[in] lhs Left hand side
  /// @param[in] rhs Right side
  /// @return Concatenated paths
  [[nodiscard]] friend Path operator/(const Path& lhs, std::string_view rhs)
  {
    Path result{lhs.memory_resource_};
    result.path_.reserve(lhs.path_.size() + rhs.size() + 1U);
    result.path_.append(lhs.path_);
    result /= rhs;
    return result;
  }
  /// Comparison operator
  /// @param[in] lhs Left hand side operand
  /// @param[in] rhs Right hand side operand
  /// @return True iff lhs == rhs
  [[nodiscard]] friend bool operator==(const Path& lhs, const Path& rhs) noexcept
  {
    return lhs.path_ == rhs.path_;
  }

  /// Comparison operator with null terminated array of char
  /// @param[in] lhs Left hand side operand
  /// @param[in] rhs Right hand side operand
  /// @return True iff lhs == rhs
  [[nodiscard]] friend bool operator==(const Path& lhs, const char* rhs) noexcept
  {
    return lhs.path_ == std::string_view{rhs};
  }

  /// Comparison operator
  /// @param[in] lhs Left hand side operand
  /// @param[in] rhs Right hand side operand
  /// @return True iff lhs != rhs
  [[nodiscard]] friend bool operator!=(const Path& lhs, const Path& rhs) noexcept
  {
    return lhs.path_ != rhs.path_;
  }

  /// Comparison operator with null terminated array of char
  /// @param[in] lhs Left hand side operand
  /// @param[in] rhs Right hand side operand
  /// @return True iff lhs != rhs
  [[nodiscard]] friend bool operator!=(const Path& lhs, const char* rhs) noexcept
  {
    return lhs.path_ != std::string_view{rhs};
  }

  /// Comparison operator
  /// @param[in] lhs Left hand side operand
  /// @param[in] rhs Right hand side operand
  /// @return True iff lhs < rhs
  [[nodiscard]] friend bool operator<(const Path& lhs, const Path& rhs) noexcept
  {
    return lhs.path_ < rhs.path_;
  }

  /// Comparison operator with null terminated array of char
  /// @param[in] lhs Left hand side operand
  /// @param[in] rhs Right hand side operand
  /// @return True iff lhs < rhs
  [[nodiscard]] friend bool operator<(const Path& lhs, const char* rhs) noexcept
  {
    return lhs.path_ < std::string_view{rhs};
  }

  /// Comparison operator
  /// @param[in] lhs Left hand side operand
  /// @param[in] rhs Right hand side operand
  /// @return True iff lhs <= rhs
  [[nodiscard]] friend bool operator<=(const Path& lhs, const Path& rhs) noexcept
  {
    return lhs.path_ <= rhs.path_;
  }

  /// Comparison operator with null terminated array of char
  /// @param[in] lhs Left hand side operand
  /// @param[in] rhs Right hand side operand
  /// @return True iff lhs <= rhs
  [[nodiscard]] friend bool operator<=(const Path& lhs, const char* rhs) noexcept
  {
    return lhs.path_ <= std::string_view{rhs};
  }

  /// Comparison operator
  /// @param[in] lhs Left hand side operand
  /// @param[in] rhs Right hand side operand
  /// @return True iff lhs > rhs
  [[nodiscard]] friend bool operator>(const Path& lhs, const Path& rhs) noexcept
  {
    return lhs.path_ > rhs.path_;
  }

  /// Comparison operator with null terminated array of char
  /// @param[in] lhs Left hand side operand
  /// @param[in] rhs Right hand side operand
  /// @return True iff lhs > rhs
  [[nodiscard]] friend bool operator>(const Path& lhs, const char* rhs) noexcept
  {
    return lhs.path_ > std::string_view{rhs};
  }

  /// Comparison operator
  /// @param[in] lhs Left hand side operand
  /// @param[in] rhs Right hand side operand
  /// @return True iff lhs >= rhs
  [[nodiscard]] friend bool operator>=(const Path& lhs, const Path& rhs) noexcept
  {
    return lhs.path_ >= rhs.path_;
  }

  /// Comparison operator with null terminated array of char
  /// @param[in] lhs Left hand side operand
  /// @param[in] rhs Right hand side operand
  /// @return True iff lhs >= rhs
  [[nodiscard]] friend bool operator>=(const Path& lhs, const char* rhs) noexcept
  {
    return lhs.path_ >= std::string_view{rhs};
  }

  /// Return the parent path view
  /// @return Path to the parent directory
  [[nodiscard]] std::string_view parent_path_view() const noexcept;

  /// Return the filename view
  /// @return File name path component
  [[nodiscard]] std::string_view filename_view() const noexcept;

  /// Return the stem view
  /// @return File name path component without the final extension
  [[nodiscard]] std::string_view stem_view() const noexcept;

  /// Return the extension view
  /// @return File extension view
  [[nodiscard]] std::string_view extension_view() const noexcept;

private:
  /// Path string
  std::pmr::string path_;

  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;
};

/// Output stream insertion operator for paths
/// @param[in] ostream Output stream
/// @param[in] path Path
/// @return Output stream reference
std::ostream& operator<<(std::ostream& ostream, const Path& path);

} // namespace jewels::filesystem
