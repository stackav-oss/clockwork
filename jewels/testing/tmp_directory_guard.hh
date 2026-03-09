// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/filesystem/filesystem.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"

#include <optional>

namespace jewels::testing
{

/// Function to get the path to the bazel temporary directory, or a fallback if unavailable.
jewels::filesystem::Path get_bazel_temp_dir(jewels::memory::MemoryResource memory_resource);

/// Class to create a test directory for log testing
class TmpDirectoryGuard
{
public:
  /// Create a log test directory that is deleted when this object goes out of scope.
  /// @note This reads environment variables, so is not thread-safe.
  /// @throws std::filesystem::filesystem_error if the directory could not be created.
  explicit TmpDirectoryGuard(std::optional<jewels::memory::MemoryResource> memory_resource = std::nullopt);

  /// Delete the test directory
  ~TmpDirectoryGuard();

  TmpDirectoryGuard(const TmpDirectoryGuard&) = delete;
  TmpDirectoryGuard& operator=(const TmpDirectoryGuard&) = delete;
  TmpDirectoryGuard(TmpDirectoryGuard&&) = default;
  TmpDirectoryGuard& operator=(TmpDirectoryGuard&&) = default;

  /// Get the path to the test directory
  /// @return Directory path
  [[nodiscard]] const jewels::filesystem::Path& get_path() const;

private:
  /// Memory resource for filesystem interfacing
  jewels::memory::MemoryResource memory_resource_;
  /// Filesystem for directory management
  jewels::filesystem::Filesystem filesystem_;
  /// Test directory path
  jewels::filesystem::Path path_;
};

} // namespace jewels::testing
