// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/testing/filesystem_wrapper.hh"
#include "jewels/testing/tmp_directory_guard.hh"

#include <catch2/catch_test_macros.hpp>

#include <cerrno>
#include <filesystem>
#include <memory_resource>

namespace jewels::filesystem::testing
{

TEST_CASE("FilesystemWrapper::inject_remove_error")
{
  const ::jewels::testing::TmpDirectoryGuard temporary_directory;
  const std::string placeholder_path = temporary_directory.get_path() / "placeholder";

  const memory::MemoryResource memory_resource(std::pmr::new_delete_resource());
  FilesystemWrapper filesystem_wrapper(memory_resource);

  REQUIRE(filesystem_wrapper.create_directory(placeholder_path));

  SECTION("after 0")
  {
    filesystem_wrapper.inject_remove_error(EACCES);
  }

  SECTION("after 1")
  {
    filesystem_wrapper.inject_remove_error(EACCES, 1);
    REQUIRE(filesystem_wrapper.remove(placeholder_path));
  }

  const auto status = filesystem_wrapper.remove(placeholder_path);
  REQUIRE_FALSE(status);

  CHECK(status.error().value() == EACCES);
}

} // namespace jewels::filesystem::testing
