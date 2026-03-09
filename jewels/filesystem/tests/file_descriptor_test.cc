// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/filesystem/filesystem.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/tmp_directory_guard.hh"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <memory_resource>
#include <string>
#include <utility>

namespace jewels::filesystem::testing
{
namespace
{

TEST_CASE("FileDescriptor")
{
  const jewels::testing::TmpDirectoryGuard test_dir;
  const memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  Filesystem filesys{memory_resource};

  const auto test_file_path1 = test_dir.get_path() / "TEST_FILE1";
  auto open_result1 = filesys.open(test_file_path1.string(), O_CREAT | O_EXCL | O_WRONLY);
  REQUIRE(open_result1);
  const auto unused_fd1 = **open_result1;

  const auto test_file_path2 = test_dir.get_path() / "TEST_FILE2";
  auto open_result2 = filesys.open(test_file_path2.string(), O_CREAT | O_EXCL | O_WRONLY);
  REQUIRE(open_result2);
  const auto unused_fd2 = **open_result2;

  REQUIRE(open_result1->close());
  REQUIRE(open_result2->close());

  SECTION("Default constructor")
  {
    FileDescriptor file_desc{};
    REQUIRE_FALSE(file_desc);
    REQUIRE(*file_desc == -1);
    REQUIRE(file_desc.close());
  }

  SECTION("Constructor")
  {
    FileDescriptor file_desc{unused_fd1};
    REQUIRE(file_desc);
    REQUIRE(*file_desc == unused_fd1);
    REQUIRE(file_desc.close() == jewels::unexpected(make_error_code(EBADF)));
  }

  SECTION("Move constructor")
  {
    FileDescriptor file_desc1{unused_fd1};
    REQUIRE(file_desc1);
    REQUIRE(*file_desc1 == unused_fd1);
    auto file_desc2 = std::move(file_desc1);
    REQUIRE_FALSE(file_desc1); // NOLINT(bugprone-use-after-move) we're testing the move impl
    REQUIRE(file_desc2);
    REQUIRE(*file_desc2 == unused_fd1);
    REQUIRE(file_desc2.close() == jewels::unexpected(make_error_code(EBADF)));
  }

  SECTION("Move assignment")
  {
    FileDescriptor file_desc1{unused_fd1};
    REQUIRE(file_desc1);
    REQUIRE(*file_desc1 == unused_fd1);
    FileDescriptor file_desc2;
    file_desc2 = std::move(file_desc1);
    REQUIRE_FALSE(file_desc1); // NOLINT(bugprone-use-after-move) we're testing the move impl
    REQUIRE(file_desc2);
    REQUIRE(*file_desc2 == unused_fd1);
    REQUIRE(file_desc2.close() == jewels::unexpected(make_error_code(EBADF)));
  }

  SECTION("Release")
  {
    FileDescriptor file_desc{unused_fd1};
    REQUIRE(file_desc);
    REQUIRE(*file_desc == unused_fd1);
    REQUIRE(file_desc.release() == unused_fd1);
    REQUIRE_FALSE(file_desc);
    REQUIRE(*file_desc == -1);
  }

  SECTION("Comparison")
  {
    const FileDescriptor file_desc1{std::min(unused_fd1, unused_fd2)};
    const FileDescriptor file_desc1_2{*file_desc1};
    const FileDescriptor file_desc2{std::max(unused_fd1, unused_fd2)};
    const FileDescriptor file_desc2_2{*file_desc2};

    REQUIRE(file_desc1 == file_desc1_2);
    REQUIRE_FALSE(file_desc1 == file_desc2_2);
    REQUIRE_FALSE(file_desc2 == file_desc1_2);
    REQUIRE(file_desc2 == file_desc2_2);

    REQUIRE_FALSE(file_desc1 != file_desc1_2);
    REQUIRE(file_desc1 != file_desc2_2);
    REQUIRE(file_desc2 != file_desc1_2);
    REQUIRE_FALSE(file_desc2 != file_desc2_2);

    REQUIRE_FALSE(file_desc1 < file_desc1_2);
    REQUIRE(file_desc1 < file_desc2_2);
    REQUIRE_FALSE(file_desc2 < file_desc1_2);
    REQUIRE_FALSE(file_desc2 < file_desc2_2);
  }
}

} // namespace
} // namespace jewels::filesystem::testing
