// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/testing/tmp_directory_guard.hh"

#include "jewels/filesystem/filesystem.hh"
#include "jewels/memory/default_memory_resource.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>

namespace jewels::testing
{

namespace
{

/// Bazel test temp directory environment variable
constexpr auto* bazel_test_tmpdir_env_name = "TEST_TMPDIR";

} // namespace

jewels::filesystem::Path get_bazel_temp_dir(jewels::memory::MemoryResource memory_resource)
{
  // NOLINTNEXTLINE(concurrency-mt-unsafe) There's a warning in the documentation for this type.
  const auto* temp_dir = std::getenv(bazel_test_tmpdir_env_name);
  if (temp_dir == nullptr)
  {
    temp_dir = "/tmp";
  }
  return {std::string_view(temp_dir), memory_resource};
}

TmpDirectoryGuard::TmpDirectoryGuard(std::optional<jewels::memory::MemoryResource> memory_resource)
  : memory_resource_(memory_resource.value_or(jewels::memory::get_default_memory_resource())),
    filesystem_(memory_resource_),
    path_(memory_resource_)
{
  const auto temp_dir = get_bazel_temp_dir(memory_resource_);
  auto maybe_path = filesystem_.create_temporary_directory(temp_dir);
  if (!maybe_path)
  {
    throw std::filesystem::filesystem_error(
      "Could not create directory", std::make_error_code(std::errc::no_such_file_or_directory));
  }
  path_ = jewels::filesystem::Path(maybe_path->string_view(), memory_resource_);
}

TmpDirectoryGuard::~TmpDirectoryGuard()
{
  if (!path_.empty())
  {
    std::ignore = filesystem_.remove_all(path_);
  }
}

[[nodiscard]] const jewels::filesystem::Path& TmpDirectoryGuard::get_path() const
{
  return path_;
}

} // namespace jewels::testing
