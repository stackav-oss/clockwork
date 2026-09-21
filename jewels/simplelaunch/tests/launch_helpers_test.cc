// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/callsig/outcome.hh"
#include "jewels/filesystem/filesystem.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/simplelaunch/config.hh"
#include "jewels/simplelaunch/simplelaunch_impl.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/tmp_directory_guard.hh"

#include <catch2/catch_test_macros.hpp>
#include <google/protobuf/repeated_ptr_field.h>

#include <cstdint>
#include <memory_resource>
#include <optional>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <vector>

namespace jewels::simplelaunch
{

TEST_CASE("Argv Preparation")
{
  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  jewels::filesystem::Filesystem fsys{memres};
  const jewels::testing::TmpDirectoryGuard tmp_guard;
  const auto work_dir = fsys.create_temporary_directory(tmp_guard.get_path());
  REQUIRE(work_dir);

  AppConfig config;
  const auto bin_path = tmp_guard.get_path() / "foo";
  uint32_t bin_perms{S_IRUSR | S_IRGRP | S_IROTH | S_IXUSR | S_IXGRP | S_IXOTH};
  REQUIRE(fsys.touch(bin_path, bin_perms));
  config.set_executable(bin_path.string_view());
  config.mutable_args()->Add("bar");

  std::vector<std::string> argv;
  SECTION("Non-Root")
  {
    REQUIRE(ok(make_argv(config, memres, fsys, *work_dir, argv)));
    REQUIRE(argv.size() == 2);
    CHECK(argv.at(0) == bin_path.string_view());
    CHECK(argv.at(1) == "bar");
  }

  SECTION("As Root")
  {
    config.set_as_root(true);
    REQUIRE(ok(make_argv(config, memres, fsys, *work_dir, argv)));
    REQUIRE(argv.size() == 5);
    CHECK(argv.at(0).find("sudo") != std::string::npos);
    CHECK(argv.at(1) == "--preserve-env");
    CHECK(argv.at(2) == "--non-interactive");

    const auto moved_bin = *work_dir / "foo";
    CHECK(argv.at(3) == moved_bin.string_view());
    struct stat statbuf{};
    REQUIRE(::stat(moved_bin.c_str(), &statbuf) == 0);
    const auto masked_mode = statbuf.st_mode & bin_perms;
    CHECK(masked_mode == bin_perms);

    CHECK(argv.at(4) == "bar");
  }
}

} // namespace jewels::simplelaunch
