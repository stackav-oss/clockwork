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

namespace clockwork::dsl::composition::tests
{
// Simple test to verify that a Clockwork system with signals starts up and runs without errors.
TEST_CASE("Signals Test System Startup")
{
  using namespace std::chrono_literals;
  const auto process_description_path = jewels::fix_clockwork_path(
    "clockwork/dsl/composition/tests/support/"
    "clockwork.clockwork.dsl.composition.tests.support.signals_test_system_target.SignalTestSystem.proc.tachyon");

  constexpr auto start_time = jewels::time::SyncTime(3000ms);
  constexpr auto end_time = start_time + 100ms;

  auto config = ClockworkSystemRunnerConfig{
    .process_description_path = process_description_path, .start_time = start_time, .end_time = end_time};
  auto system_runner = ClockworkSystemRunner::create(config);
  REQUIRE(system_runner);
  REQUIRE(system_runner->run());
}
} // namespace clockwork::dsl::composition::tests
