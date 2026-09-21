// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/test_tools/clockwork_system_runner.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/utility/fix_clockwork_path.hh"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <optional>
#include <string>

namespace clockwork
{
TEST_CASE("Test Clockwork system runner no logs")
{
  using namespace std::chrono_literals;
  const auto process_description_path = jewels::fix_clockwork_path(
    "clockwork/test_tools/tests/support/"
    "clockwork.clockwork.test_tools.tests.support.addition_test_system.addition_system.proc.tachyon");

  constexpr auto start_time = jewels::time::SyncTime(3000ms);
  constexpr auto end_time = start_time + 300ms;
  SECTION("Valid Configuration")
  {
    auto config = ClockworkSystemRunnerConfig{
      .process_description_path = process_description_path, .start_time = start_time, .end_time = end_time};
    auto system_runner = ClockworkSystemRunner::create(config);
    REQUIRE(system_runner);
    REQUIRE(system_runner->run());
  }
  SECTION("No start time set")
  {
    auto config =
      ClockworkSystemRunnerConfig{.process_description_path = process_description_path, .end_time = end_time};
    auto system_runner = ClockworkSystemRunner::create(config);
    REQUIRE(!system_runner);
  }
  SECTION("Invalid process description path")
  {
    auto config = ClockworkSystemRunnerConfig{
      .process_description_path = "/tmp/junk.tachyon", .start_time = start_time, .end_time = end_time};
    auto system_runner = ClockworkSystemRunner::create(config);
    REQUIRE(!system_runner);
  }
}
} // namespace clockwork
