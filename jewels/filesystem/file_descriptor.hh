// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/filesystem/error_code.hh"
#include "jewels/std/expected.hh"

#include <cstdint>
#include <iosfwd>

namespace jewels::filesystem
{

/// RAII file descriptor wrapper
class FileDescriptor
{
public:
  /// Construct a FileDescriptor with ownership of a file descriptor
  explicit FileDescriptor(int32_t file_desc) noexcept;

  constexpr FileDescriptor() noexcept = default;

  /// Destructor closes the owned file
  ~FileDescriptor() noexcept;

  FileDescriptor(const FileDescriptor&) = delete;
  FileDescriptor& operator=(const FileDescriptor&) = delete;

  /// Move constructor transfers ownership of the file descriptor
  /// @param[in,out] other Move source
  FileDescriptor(FileDescriptor&& other) noexcept;

  /// Move assignment takes ownership of the file descriptor
  /// @param[in,out] other Move source
  FileDescriptor& operator=(FileDescriptor&& other) noexcept;

  /// Test whether the file descriptor is valid
  /// @return True if the file descriptor is valid
  [[nodiscard]] explicit operator bool() const noexcept;

  /// Accessor for the file descriptor
  /// @return File descriptor
  [[nodiscard]] int32_t operator*() const noexcept;

  /// Release the file descriptor
  /// @return File number
  [[nodiscard]] int32_t release() noexcept;

  /// Close the file descriptor
  /// @return Error condition on failure
  [[nodiscard]] jewels::expected<void, ErrorCode> close() noexcept;

  /// Close the file descriptor ignoring errors
  void forced_close() noexcept;

  /// Comparison operator
  /// @param[in] lhs Left hand side operand
  /// @param[in] rhs Right hand side operand
  /// @return True iff lhs == rhs
  [[nodiscard]] friend bool operator==(const FileDescriptor& lhs, const FileDescriptor& rhs) noexcept
  {
    return lhs.file_desc_ == rhs.file_desc_;
  }

  /// Comparison operator
  /// @param[in] lhs Left hand side operand
  /// @param[in] rhs Right hand side operand
  /// @return True iff lhs != rhs
  [[nodiscard]] friend bool operator!=(const FileDescriptor& lhs, const FileDescriptor& rhs) noexcept
  {
    return !(lhs == rhs);
  }

  /// Comparison operator
  /// @param[in] lhs Left hand side operand
  /// @param[in] rhs Right hand side operand
  /// @return True iff lhs < rhs
  [[nodiscard]] friend bool operator<(const FileDescriptor& lhs, const FileDescriptor& rhs) noexcept
  {
    return lhs.file_desc_ < rhs.file_desc_;
  }

private:
  /// File descriptor, valid if greater than or equal to zero
  int32_t file_desc_{-1};
};

/// Output stream insertion operator for file descriptors
/// @param[in] ostream Output stream
/// @param[in] file_desc File descriptor
/// @return Output stream reference
std::ostream& operator<<(std::ostream& ostream, const FileDescriptor& file_desc);

} // namespace jewels::filesystem
