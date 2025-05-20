// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <filesystem>

namespace jewels::testing
{

/// Class to create a test directory for log testing
class TmpDirectoryGuard
{
public:
  /// Create a log test directory that is deleted when this object goes out of scope
  /// @note This reads environment variables, so is not thread-safe.
  TmpDirectoryGuard();

  /// Delete the test directory
  ~TmpDirectoryGuard();

  TmpDirectoryGuard(const TmpDirectoryGuard&) = delete;
  TmpDirectoryGuard& operator=(const TmpDirectoryGuard&) = delete;
  TmpDirectoryGuard(TmpDirectoryGuard&&) = default;
  TmpDirectoryGuard& operator=(TmpDirectoryGuard&&) = default;

  /// Get the path to the test directory
  /// @return Directory path
  [[nodiscard]] const std::filesystem::path& get_path() const;

private:
  /// Test directory path
  std::filesystem::path path_;
};

} // namespace jewels::testing
