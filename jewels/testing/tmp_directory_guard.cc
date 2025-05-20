// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/testing/tmp_directory_guard.hh"

#include <boost/filesystem.hpp>

#include <cstdlib>

namespace jewels::testing
{

namespace
{

/// Bazel test temp directory environment variable
constexpr auto* bazel_test_tmpdir_env_name = "TEST_TMPDIR";

} // namespace

TmpDirectoryGuard::TmpDirectoryGuard()
{
  // NOLINTNEXTLINE(concurrency-mt-unsafe) There's a warning in the documentation for this type.
  const auto* temp_dir = std::getenv(bazel_test_tmpdir_env_name);
  if (temp_dir == nullptr)
  {
    temp_dir = "/tmp";
  }
  path_ = (boost::filesystem::path(temp_dir) / boost::filesystem::unique_path()).string();
  std::filesystem::create_directories(path_);
}

TmpDirectoryGuard::~TmpDirectoryGuard()
{
  if (!path_.empty())
  {
    std::filesystem::remove_all(path_);
  }
}

[[nodiscard]] const std::filesystem::path& TmpDirectoryGuard::get_path() const
{
  return path_;
}

} // namespace jewels::testing
