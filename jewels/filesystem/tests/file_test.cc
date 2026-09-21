// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/tmp_directory_guard.hh"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace jewels::filesystem
{

TEST_CASE("file_test | sysfs directory + file")
{
  auto expected_directory = Directory::open("/proc");
  REQUIRE(expected_directory);
  auto directory = std::move(expected_directory).value();

  // Check /proc/self/cmdline by walking the directory tree
  bool found_cpuinfo = false;
  bool found_cmdline = false;
  auto expected_result = directory.process(
    [&](int dirfd, std::string_view name, uint8_t /*type*/) -> jewels::expected<bool, ErrorCode>
    {
      if (name == "cpuinfo")
      {
        found_cpuinfo = true;
      }
      else if (name == "self")
      {
        Directory self(dirfd, name);
        auto expected_result2 = self.process(
          [&](int dirfd2, std::string_view name2, uint8_t /*type*/) -> jewels::expected<bool, ErrorCode>
          {
            if (name2 == "cmdline")
            {
              File cmdline(dirfd2, name2);
              std::array<char, 1024> buffer{};
              auto read_result = cmdline.pread_str(buffer);
              REQUIRE(read_result);
              CHECK(strstr(buffer.data(), "file_test") != nullptr);
              found_cmdline = true;
              return false;
            }
            return true;
          });
        CHECK(expected_result2);
      }
      return true;
    });
  REQUIRE(expected_result);
  CHECK(found_cpuinfo);
  CHECK(found_cmdline);
}

TEST_CASE("file_test | file directory flags")
{
  REQUIRE(File::open("/proc/self/exe"));
  CHECK(!Directory::open("/proc/self/exe"));

  CHECK(Directory::open("/proc/self"));
  CHECK(!Directory::open("/proc/self", O_RDWR));
}

TEST_CASE("file_test | make directory")
{
  const jewels::testing::TmpDirectoryGuard tmpdir_obj;
  auto tmpdir = Directory::open(tmpdir_obj.get_path().c_str());
  REQUIRE(tmpdir);
  CHECK(!Directory::open(tmpdir->descriptor(), "a"));

  auto dir_a = Directory::create_open(tmpdir->descriptor(), "a");
  REQUIRE(dir_a);
  // Creating a "path" should fail
  CHECK(!Directory::create_open(tmpdir->descriptor(), "a/b/c"));
  // The recursive API should work
  CHECK(Directory::create_open(tmpdir->descriptor(), "a", "b", "c"));
  // Check that the directory now exists
  CHECK(Directory::open(tmpdir->descriptor(), "a/b/c"));
  CHECK(Directory::open(dir_a->descriptor(), "b/c"));
  // "recreating" the same directory should work too
  CHECK(Directory::create_open(tmpdir->descriptor(), "a", "b", "c"));
  CHECK(Directory::create_open(dir_a->descriptor(), "b", "c"));
  // Now that it exists, create_open of the path should succeed using the non-recursive version
  CHECK(Directory::create_open(tmpdir->descriptor(), "a/b/c"));
  // Create a path where only the last name is missing
  CHECK(Directory::create_open(tmpdir->descriptor(), "a/b/c/d"));
  CHECK(Directory::open(tmpdir->descriptor(), "a/b/c/d"));

  // Test use of at_cwd to provide a "don't care" origin path to create in the tmpdir
  CHECK(Directory::create_open(Directory::at_cwd, tmpdir_obj.get_path().c_str(), "x", "y", "z"));
  // Check that the directory exists in the tmpdir
  CHECK(Directory::open(tmpdir->descriptor(), "x/y/z"));

  SECTION("accepts non null terminated string_view names")
  {
    const std::string backed_name = "view_name trailing bytes";
    const std::string_view name{backed_name.data(), std::string_view{"view_name"}.size()};

    auto created_dir = Directory::create_open(tmpdir->descriptor(), name);
    REQUIRE(created_dir);
    CHECK(Directory::open(tmpdir->descriptor(), "view_name"));
    CHECK(!Directory::open(tmpdir->descriptor(), backed_name));
  }
}

TEST_CASE("file_test | file reading")
{
  const jewels::testing::TmpDirectoryGuard tmpdir_obj;
  const Directory tmpdir{tmpdir_obj.get_path().c_str()};

  // NOLINTNEXTLINE(modernize-avoid-c-arrays) needs a C array to be able to have a static knowable size with a \0.
  constexpr const char testdata_raw[] = "abc\0efg";
  constexpr size_t null_pos = 3;
  constexpr size_t buffer_size = sizeof(testdata_raw) + 3;
  const auto testdata = std::string_view(static_cast<const char*>(testdata_raw), sizeof(testdata_raw) - 1);
  const auto testdata_str = std::string_view(static_cast<const char*>(testdata_raw));
  CHECK(testdata != testdata_str);
  CHECK(testdata[null_pos] == 0);

  {
    const File file{tmpdir.descriptor(), "test", O_CREAT | O_WRONLY};
    CHECK(::write(file.descriptor(), testdata.data(), testdata.size()) == static_cast<ssize_t>(testdata.size()));
  }

  File file(tmpdir.descriptor(), "test");
  SECTION("pread")
  {
    std::array<std::byte, buffer_size> buffer{};
    for (size_t limit = 0; limit < buffer_size; limit++)
    {
      auto read = file.pread(std::span(buffer).subspan(0, limit)).value();
      CHECK(read == std::min(limit, testdata.size()));
      CHECK(::memcmp(buffer.data(), testdata.data(), read) == 0);
    }
  }
  SECTION("read_all")
  {
    const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
    SECTION("normal")
    {
      auto data = file.read_all(memory_resource);
      REQUIRE(data);
      CHECK(data->size() == testdata.size());
      CHECK(::memcmp(data->data(), testdata.data(), testdata.size()) == 0);

      CHECK(file.read_all(memory_resource, testdata.size()));
      CHECK(!file.read_all(memory_resource, testdata.size() - 1));
    }
    SECTION("special")
    {
      // This may be system dependant to an extent, but most /proc files have a 0 size reported which doesn't work for
      // this operation.
      const auto result = File("/proc/self/cmdline").read_all(memory_resource);
      REQUIRE(!result);
      CHECK(result.error().value() == EIO);
    }
    SECTION("after reads")
    {
      std::array<char, buffer_size> buffer{};
      CHECK(::read(file.descriptor(), buffer.data(), 1) == 1);
      CHECK(buffer[0] == testdata[0]);
      CHECK(::read(file.descriptor(), buffer.data(), 1) == 1);
      CHECK(buffer[0] == testdata[1]);

      auto data = file.read_all(memory_resource);
      REQUIRE(data);
      CHECK(data->size() == testdata.size());
      CHECK(::memcmp(data->data(), testdata.data(), testdata.size()) == 0);
    }
  }
}

} // namespace jewels::filesystem
