// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/filesystem/file.hh"
#include "jewels/filesystem/filesystem.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/simplelaunch/simplelaunch_impl.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/tmp_directory_guard.hh"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <memory_resource>
#include <string>
#include <string_view>
#include <vector>

namespace jewels::simplelaunch
{

TEST_CASE("RedirectOutputHelper")
{
  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  jewels::filesystem::Filesystem fsys{memres};
  const jewels::testing::TmpDirectoryGuard tmp_dir;
  const auto log_dir = tmp_dir.get_path() / "foo";

  // Note: The cout and cerr ostreams are still hanging on to a reference to
  // the original stdio file descriptors after RedirectOutputHelper dups them,
  // so their writes fall into the void until the redirector goes out of
  // scope. So, we use stdout and stderr below instead.

  // flush stdout and stderr so we don't get output from catch2 in the log
  REQUIRE(fflush(stdout) == 0);
  REQUIRE(fflush(stderr) == 0);
  const auto redirector = RedirectOutputHelper::make(log_dir, fsys);
  REQUIRE(redirector);
  const auto log_path = redirector->log_path();
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) this is a test and the usage is straightforward
  REQUIRE(fprintf(stdout, "foo\n") > 0);
  REQUIRE(fflush(stdout) == 0);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) this is a test and the usage is straightforward
  REQUIRE(fprintf(stderr, "bar\n") > 0);
  REQUIRE(fflush(stderr) == 0);

  auto check_result = fsys.is_regular_file(log_path.string_view());
  REQUIRE(check_result);
  REQUIRE(*check_result);

  auto log = jewels::filesystem::File::open(log_path.string_view());
  REQUIRE(log);
  auto log_contents = log->read_all(memres);
  REQUIRE(log_contents);

  const std::string_view expected{"foo\nbar\n"};
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) byte and char are compatible types
  const std::string_view actual{reinterpret_cast<char*>(log_contents->data()), log_contents->size()};
  REQUIRE(actual == expected);
}

} // namespace jewels::simplelaunch
