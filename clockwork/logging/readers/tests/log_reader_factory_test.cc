// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/readers/abstract_log_reader.hh"
#include "clockwork/logging/readers/log_reader_factory.hh"
#include "clockwork/logging/readers/types.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/testing/tmp_directory_guard.hh"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <fmt/format.h>

#include <fstream>
#include <memory>
#include <memory_resource>
#include <optional>
#include <string>

namespace clockwork_logging
{
namespace
{

TEST_CASE("Log reader factory")
{
  SECTION("mcap")
  {
    auto reader = make_reader("log.mcap", {}, {});
    REQUIRE(reader);

    REQUIRE("mcap" == reader->type());
  }

  SECTION("onboard")
  {
    const jewels::testing::TmpDirectoryGuard test_dir;
    const auto& log_path = test_dir.get_path();
    const auto onboard_file_path = log_path / "foo.olog";
    (void)std::ofstream{onboard_file_path.c_str()};

    auto reader = make_reader(log_path.string(), {}, {});
    REQUIRE(reader);

    REQUIRE(reader->type() == "onboard");
  }

  SECTION("onboard with offboard URI")
  {
    const jewels::testing::TmpDirectoryGuard test_dir;
    const auto& log_path = test_dir.get_path();
    const auto onboard_file_path = log_path / "foo.olog";
    (void)std::ofstream{onboard_file_path.c_str()};

    auto reader = make_reader(fmt::format("file:{}", log_path.string()), {}, {});
    REQUIRE(reader);

    REQUIRE(reader->type() == "onboard");
  }

  SECTION("offboard log")
  {
    const jewels::testing::TmpDirectoryGuard test_dir;
    const auto& log_path = test_dir.get_path();
    const auto offboard_file_path = log_path / "stack_log_metadata.pbtxt";
    (void)std::ofstream{offboard_file_path.c_str()};

    auto reader = make_reader(log_path.string(), {}, {});
    REQUIRE(reader);

    REQUIRE(reader->type() == "offboard");
  }

  SECTION("offboard log union")
  {
    const jewels::testing::TmpDirectoryGuard test_dir;
    const auto& log_path = test_dir.get_path();
    const auto log_union_path = log_path / "stack_log_union.pbtxt";
    (void)std::ofstream{log_union_path.c_str()};

    auto reader = make_reader(log_path.string(), {}, {});
    REQUIRE(reader);

    REQUIRE(reader->type() == "offboard");
  }

  SECTION("offboard log amendment")
  {
    const jewels::testing::TmpDirectoryGuard test_dir;
    const auto& log_path = test_dir.get_path();
    const auto log_amendment_path = log_path / "stack_log_amendment.pbtxt";
    (void)std::ofstream{log_amendment_path.c_str()};

    auto reader = make_reader(log_path.string(), {}, {});
    REQUIRE(reader);

    REQUIRE(reader->type() == "offboard");
  }

  SECTION("offboard log missing metadata file")
  {
    const jewels::testing::TmpDirectoryGuard test_dir;
    const auto& log_path = test_dir.get_path();
    const auto slog_file_path = log_path / "log_file.slog";
    (void)std::ofstream{slog_file_path.c_str()};

    auto reader = make_reader(log_path.string(), {}, {});
    REQUIRE(reader);

    REQUIRE(reader->type() == "offboard");
  }

  SECTION("unknown")
  {
    using Catch::Matchers::StartsWith;

    REQUIRE_THROWS_WITH(make_reader("log.unknown", {}, {}), StartsWith("Unknown/Unsupported log 'log.unknown'"));
  }
}

} // namespace
} // namespace clockwork_logging
