// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/filesystem/filesystem.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/default_memory_resource.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/tmp_directory_guard.hh"

#include <catch2/catch_test_macros.hpp>

#include <optional>

namespace jewels::testing
{

TEST_CASE("get_bazel_temp_dir returns a valid directory")
{
  const auto memres = jewels::memory::get_default_memory_resource();
  const auto temp_dir = get_bazel_temp_dir(memres);
  const auto fsys = jewels::filesystem::Filesystem(memres);

  auto check_result = fsys.is_directory(temp_dir.string_view());
  REQUIRE(check_result);
  REQUIRE(*check_result);
}

TEST_CASE("TmpDirectoryGuard creates and deletes a temporary directory")
{
  const auto memres = jewels::memory::get_default_memory_resource();
  const auto temp_dir = get_bazel_temp_dir(memres);
  const auto fsys = jewels::filesystem::Filesystem(memres);

  // Create a temporary directory and check that it exists
  auto guard = std::make_optional(TmpDirectoryGuard(memres));
  REQUIRE(guard);
  const auto& dir_path = guard->get_path();
  auto check_result = fsys.is_directory(dir_path.string_view());
  REQUIRE(check_result);
  REQUIRE(*check_result);

  // Destroy the guard and check that the directory has been removed
  guard = std::nullopt;
  check_result = fsys.exists(dir_path.string_view());
  REQUIRE(check_result);
  REQUIRE(!*check_result);
}
} // namespace jewels::testing
