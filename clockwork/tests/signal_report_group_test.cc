// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/decompress_option.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/readers/offboard_log_reader.hh"
#include "clockwork/logging/readers/types.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/test_tools/clockwork_system_runner.hh"
#include "clockwork/tests/support/signal_cog_clk_cc.hh"
#include "clockwork/tests/support/signal_cog_clk_cc_dial.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/container/compare.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/std/expected.hh"
#include "jewels/std/span.hh"
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/utility/fix_clockwork_path.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace clockwork
{
TEST_CASE("Signal report groups are published via system runner", "[signals][report_groups][csr]")
{
  using namespace std::chrono_literals;
  const auto process_description_path = jewels::fix_clockwork_path(
    "clockwork/tests/support/"
    "clockwork.clockwork.tests.support.signal_test_system.signal_test_system.proc.tachyon");

  const auto event_logger_config_path = jewels::fix_clockwork_path(
    "clockwork/tests/support/"
    "signal_test_system.signal_test_system.SignalTestCpu_event_logger_config.tachyon");

  // Run for 2 seconds: at 10Hz that gives ~20 trigger messages,
  // which is enough to exceed max_observations=5 multiple times
  // and trigger report group publishing for the post-aggregated group.
  constexpr auto start_time = jewels::time::SyncTime(3000ms);
  constexpr auto end_time = start_time + 2000ms;

  SECTION("System runs successfully with signal report groups")
  {
    auto config = ClockworkSystemRunnerConfig{
      .process_description_path = process_description_path, .start_time = start_time, .end_time = end_time};
    auto system_runner = ClockworkSystemRunner::create(config);
    REQUIRE(system_runner);
    REQUIRE(system_runner->run());
  }

  SECTION("Report group channel is registered in the output log")
  {
    jewels::testing::TmpDirectoryGuard tmp_dir;
    const auto output_log_path = (tmp_dir.get_path() / "output_log").string();

    auto config = ClockworkSystemRunnerConfig{
      .process_description_path = process_description_path,
      .start_time = start_time,
      .end_time = end_time,
      .output_log_config = LogConfig(output_log_path, event_logger_config_path)};

    auto system_runner = ClockworkSystemRunner::create(config);
    REQUIRE(system_runner);
    REQUIRE(system_runner->run());

    // Read back the output log and verify the report group channel was created
    clockwork_logging::OffboardLogReader log_reader(
      output_log_path, {}, {}, clockwork_logging::DecompressOption::decompress);

    REQUIRE(log_reader.open({}));

    const auto channels = log_reader.get_channels();

    // Find channels matching the report group naming convention:
    // /_clockwork/report-groups/{CogName}/{group_name}/{uuid}
    std::vector<std::string> report_group_channels;
    for (const auto& channel : channels)
    {
      if (channel.find("report-groups") != std::string::npos)
      {
        report_group_channels.push_back(channel);
      }
    }

    // At least one report group channel must exist
    REQUIRE_FALSE(report_group_channels.empty());

    // Verify the channel follows the expected naming: contains SignalCog/test_group
    const auto has_expected_channel = std::any_of(
      report_group_channels.begin(),
      report_group_channels.end(),
      [](const std::string& channel)
      { return channel.find("report-groups/SignalCog/test_group") != std::string::npos; });
    CHECK(has_expected_channel);

    // Verify the batched group channel is also present
    const auto has_batched_channel = std::any_of(
      report_group_channels.begin(),
      report_group_channels.end(),
      [](const std::string& channel)
      { return channel.find("report-groups/SignalCog/batched_group") != std::string::npos; });
    CHECK(has_batched_channel);

    REQUIRE(log_reader.close());
  }

  SECTION("Report group messages contain valid signal data")
  {
    jewels::testing::TmpDirectoryGuard tmp_dir;
    const auto output_log_path = (tmp_dir.get_path() / "output_log").string();

    auto config = ClockworkSystemRunnerConfig{
      .process_description_path = process_description_path,
      .start_time = start_time,
      .end_time = end_time,
      .output_log_config = LogConfig(output_log_path, event_logger_config_path)};

    auto system_runner = ClockworkSystemRunner::create(config);
    REQUIRE(system_runner);
    REQUIRE(system_runner->run());

    // Read report group messages from the output log
    clockwork_logging::OffboardLogReader log_reader(
      output_log_path, {}, {}, clockwork_logging::DecompressOption::decompress);
    REQUIRE(log_reader.open([](std::string_view channel)
                            { return channel.find("report-groups/SignalCog/test_group") != std::string_view::npos; }));

    // Collect all report group messages
    using ReportGroupTap = Tappy<system_runner::SignalCog_test_group>;
    std::vector<ReportGroupTap> messages;
    while (auto logged_msg = log_reader.next_message())
    {
      auto& msg = messages.emplace_back();
      auto buf = std::as_writable_bytes(jewels::as_single_item_span(msg));
      std::ranges::copy(logged_msg->data, buf.begin());
    }
    REQUIRE(log_reader.close());

    // At 10Hz for 2 seconds = ~20 executions, max_observations=5, so expect at least 2 report groups
    REQUIRE(messages.size() >= 2);

    for (const auto& msg : messages)
    {
      // Each published group must have the expected execution count (= max_observations)
      CHECK(msg.get_execution_count() == 5);

      // Execution interval must be positive (time elapsed during observations)
      CHECK(msg.get_execution_interval().count() > 0);

      // tracked_value is set to trigger.value which increments from 1.
      // Post-agg min <= max must hold.
      CHECK(msg.get_tracked_value_value_min() > 0);
      CHECK(msg.get_tracked_value_value_max() >= msg.get_tracked_value_value_min());

      // accumulated_value uses sum pre-agg, with max and mean post-agg.
      // The sum per execution > 0 since secondary_value > 0, so max of sums > 0.
      CHECK(msg.get_accumulated_value_sum_max() > 0);
      CHECK(msg.get_accumulated_value_sum_mean() > 0.0);
    }
  }

  SECTION("Signal API provides correct aggregation methods")
  {
    // Validate the generated signal API directly (without going through the cog execution).
    // This tests that the code generator produces correct aggregator types and methods.
    system_runner::SignalCogDialSignalApi signal_api;
    using Policy = system_runner::SignalCogPolicy;

    const auto exec_time_1 = jewels::time::SyncTime(100ms);

    // Simulate 5 executions (matches max_observations=5 in the policy)
    for (uint32_t i = 1; i <= 5; ++i)
    {
      const auto exec_start = exec_time_1 + std::chrono::milliseconds(i * 10);
      const auto exec_end = exec_start + 1ms;

      Policy::start_of_execution_signals(signal_api, exec_start);

      signal_api.set_tracked_value(static_cast<int64_t>(i) * 10);
      signal_api.accumulate_accumulated_value(i);

      Policy::end_of_execution_signals(signal_api, exec_end);
    }

    // After 5 executions, execution count should match
    CHECK(signal_api.get_execution_count_test_group() == 5);

    // Verify post-aggregated values
    int64_t tracked_min = 0;
    int64_t tracked_max = 0;
    CHECK(jewels::ok(signal_api.get_tracked_value_value_min(jewels::Out{tracked_min})));
    CHECK(jewels::ok(signal_api.get_tracked_value_value_max(jewels::Out{tracked_max})));
    CHECK(tracked_min == 10); // min of {10, 20, 30, 40, 50}
    CHECK(tracked_max == 50); // max of {10, 20, 30, 40, 50}

    uint32_t accumulated_max = 0;
    CHECK(jewels::ok(signal_api.get_accumulated_value_sum_max(jewels::Out{accumulated_max})));
    // sum pre-agg produces {1, 2, 3, 4, 5} per execution; max post-agg of those is 5
    CHECK(accumulated_max == 5);
  }

  SECTION("Batched report group messages are published")
  {
    jewels::testing::TmpDirectoryGuard tmp_dir;
    const auto output_log_path = (tmp_dir.get_path() / "output_log").string();

    auto config = ClockworkSystemRunnerConfig{
      .process_description_path = process_description_path,
      .start_time = start_time,
      .end_time = end_time,
      .output_log_config = LogConfig(output_log_path, event_logger_config_path)};

    auto system_runner = ClockworkSystemRunner::create(config);
    REQUIRE(system_runner);
    REQUIRE(system_runner->run());

    // Read batched report group messages from the output log
    clockwork_logging::OffboardLogReader log_reader(
      output_log_path, {}, {}, clockwork_logging::DecompressOption::decompress);
    REQUIRE(
      log_reader.open([](std::string_view channel)
                      { return channel.find("report-groups/SignalCog/batched_group") != std::string_view::npos; }));

    // Collect all batched report group messages
    using BatchedReportGroupTap = Tappy<system_runner::SignalCog_batched_group>;
    std::vector<BatchedReportGroupTap> messages;
    while (auto logged_msg = log_reader.next_message())
    {
      auto& msg = messages.emplace_back();
      auto buf = std::as_writable_bytes(jewels::as_single_item_span(msg));
      std::ranges::copy(logged_msg->data, buf.begin());
    }
    REQUIRE(log_reader.close());

    // At 10Hz for 2 seconds = ~20 executions, batch_size=3, so expect at least 4 batched groups
    REQUIRE(messages.size() >= 4);

    for (const auto& msg : messages)
    {
      // Each published batch must have the expected number of signal entries (max_observations = 3)
      CHECK(msg.get_signals().size() == 3);

      // Execution interval must be positive (time elapsed during observations)
      CHECK(msg.get_execution_interval().count() > 0);
    }
  }

  SECTION("Batched signal API provides correct aggregation methods")
  {
    system_runner::SignalCogDialSignalApi signal_api;
    using Policy = system_runner::SignalCogPolicy;

    const auto exec_time_1 = jewels::time::SyncTime(100ms);

    // Simulate 3 executions (matches max_observations=3 for the batched policy)
    for (uint32_t i = 1; i <= 3; ++i)
    {
      const auto exec_start = exec_time_1 + std::chrono::milliseconds(i * 10);
      const auto exec_end = exec_start + 1ms;

      Policy::start_of_execution_signals(signal_api, exec_start);

      signal_api.accumulate_batched_value(static_cast<int64_t>(i) * 10);
      signal_api.accumulate_batched_count(i);

      Policy::end_of_execution_signals(signal_api, exec_end);
    }

    // After 3 executions, batch count should match
    CHECK(signal_api.get_batch_count_batched_group() == 3);
  }
}
} // namespace clockwork
