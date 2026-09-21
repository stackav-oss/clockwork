// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/exec_tools.hh"
#include "jewels/filesystem/file.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/std/span.hh"
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/time/sync_time.hh"

#include <catch2/catch_test_macros.hpp>
#include <tclap/CmdLine.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <fcntl.h>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace clockwork
{
namespace
{

TEST_CASE("PinionArgs")
{
  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};

  TCLAP::CmdLine cmd("testing", ' ', "1.0", true);
  const PinionArgs pinion_args{memres, cmd};

  const jewels::testing::TmpDirectoryGuard tmpdir;
  const std::string arg_pinion_dir{tmpdir.get_path().c_str()};
  const std::string arg_pinion_ns = "ns12345";

  std::array args = {
    "test.bin",
    "--pinion-dir",
    arg_pinion_dir.c_str(),
    "--pinion-ns",
    arg_pinion_ns.c_str(),
    "--pinion-resume",
    "no_resume"};
  cmd.parse(args.size(), args.data());

  auto factory = pinion_args.make_factory({});
  REQUIRE(factory);

  const std::string factory_suffix = "/clockwork/" + arg_pinion_ns + "/pinion/pub";
  struct stat stat_dir = {};
  auto tmpdir_dir = jewels::filesystem::Directory::open(arg_pinion_dir + factory_suffix);
  REQUIRE(tmpdir_dir);
  CHECK(!fstat(tmpdir_dir->descriptor(), &stat_dir));
}

TEST_CASE("ExecutionArgs")
{
  TCLAP::CmdLine cmd("testing", ' ', "1.0", true);
  const ExecutionArgs execution_args{cmd};

  const jewels::testing::TmpDirectoryGuard tmpdir;
  constexpr int64_t start_time_ns = 100000000000;
  constexpr int64_t end_time_ns = 200000000000;
  const std::string start_time_ns_string = std::to_string(start_time_ns);
  const std::string end_time_ns_string = std::to_string(end_time_ns);
  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};

  SECTION("Without log file")
  {
    std::array<const char*, 6> args = {
      {"clockwork.bin",
       "--deterministic-runner",
       "--sim-start-time-ns",
       start_time_ns_string.c_str(),
       "--sim-end-time-ns",
       end_time_ns_string.c_str()}};

    cmd.parse(args.size(), args.data());

    auto execution_params = execution_args.make_execution_params();
    REQUIRE(execution_params);
    CHECK(execution_params->execution_mode == ExecutionMode::deterministic);
    CHECK(execution_params->start_time == jewels::time::SyncTime(std::chrono::nanoseconds(start_time_ns)));
    CHECK(execution_params->end_time == jewels::time::SyncTime(std::chrono::nanoseconds(end_time_ns)));
  }
}
TEST_CASE("read_tachyon_config")
{
  const jewels::testing::TmpDirectoryGuard tmpdir;
  const auto file_path = tmpdir.get_path() / "file1";

  struct Data
  {
    float x;
    int y;
  };
  Data input{.x = 1.7f, .y = 1234};

  {
    const auto bytes = std::as_bytes(jewels::as_single_item_span(input));
    const jewels::filesystem::File file{file_path, O_CREAT | O_WRONLY};
    REQUIRE(::write(file.descriptor(), bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size()));
  }
  SECTION("read tachyon config to stack")
  {
    auto output = read_tachyon_config<Data>(file_path);
    REQUIRE(output);
    CHECK(output->x == input.x);
    CHECK(output->y == input.y);

    auto output_missing = read_tachyon_config<Data>(tmpdir.get_path() / "missing");
    REQUIRE(!output_missing);

    struct DataX
    {
      int16_t x;
      int64_t y;
    };
    auto output_wrong = read_tachyon_config<DataX>(file_path);
    REQUIRE(!output_wrong);
  }

  SECTION("read tachyon config to heap")
  {
    auto output = read_tachyon_config_to_heap<Data>(file_path);
    REQUIRE(output);
    CHECK((*output)->x == input.x);
    CHECK((*output)->y == input.y);

    auto output_missing = read_tachyon_config_to_heap<Data>(tmpdir.get_path() / "missing");
    REQUIRE(!output_missing);

    struct DataX
    {
      int16_t x;
      int64_t y;
    };
    auto output_wrong = read_tachyon_config_to_heap<DataX>(file_path);
    REQUIRE(!output_wrong);
  }
}
} // namespace
} // namespace clockwork
