// Copyright 2025 Stack AV Co.
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
TEST_CASE("Deterministic Runner Periodics")
{
  const auto process_description_path = jewels::fix_clockwork_path(
    "clockwork/tests/support/"
    "clockwork.clockwork.tests.support.test_periodic_system.test_periodic_system.proc.tachyon");

  auto config = ClockworkSystemRunnerConfig{
    .process_description_path = process_description_path,
    .start_time = jewels::time::SyncTime{},
    .end_time = jewels::time::SyncTime{std::chrono::seconds{10}},
  };

  auto system_runner = ClockworkSystemRunner::create(config);
  REQUIRE(system_runner);
  REQUIRE(system_runner->run());
}
} // namespace clockwork
