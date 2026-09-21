// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/include_common.hh"
#include "clockwork/logging/decompress_option.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/offboard/chunk_reader_writer_factory.hh"
#include "clockwork/logging/readers/offboard_log_reader.hh"
#include "clockwork/logging/readers/types.hh"
#include "clockwork/test_tools/clockwork_system_runner.hh"
#include "clockwork/tests/support/cog_metrics_custom_cog_clk_cc_dial.hh"
#include "clockwork/tests/support/cog_metrics_default_cog_clk_cc_dial.hh"
#include "jewels/container/tap/soa.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/aligned_storage.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/span.hh"
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/utility/fix_clockwork_path.hh"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <compare>
#include <cstddef>
#include <functional>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace clockwork
{
namespace
{
// Paths to generated system artifacts
const auto& process_description_path()
{
  static const auto path = jewels::fix_clockwork_path(
    "clockwork/tests/support/"
    "clockwork.clockwork.tests.support.cog_metrics_test_system.cog_metrics_test_system.proc.tachyon");
  return path;
}

const auto& event_logger_config_path()
{
  static const auto path = jewels::fix_clockwork_path(
    "clockwork/tests/support/"
    "cog_metrics_test_system.cog_metrics_test_system.CogMetricsTestCpu_event_logger_config.tachyon");
  return path;
}

// Run the system for 3 seconds: at 10Hz that gives ~30 trigger messages,
// sufficient to fill multiple batches for both default (batch=10) and
// custom (batch=5) cog event metrics report groups.
constexpr auto start_time = jewels::time::SyncTime(std::chrono::milliseconds(3000));
constexpr auto run_duration = std::chrono::milliseconds(3000);
constexpr auto end_time = start_time + run_duration;

struct LoggedSystem
{
  std::string output_log_path;
  jewels::testing::TmpDirectoryGuard tmp_dir;

  LoggedSystem()
  {
    output_log_path = (tmp_dir.get_path() / "output_log").string();
    auto config = ClockworkSystemRunnerConfig{
      .process_description_path = process_description_path(),
      .start_time = start_time,
      .end_time = end_time,
      .output_log_config = LogConfig(output_log_path, event_logger_config_path())};
    auto system_runner = ClockworkSystemRunner::create(config);
    REQUIRE(system_runner);
    REQUIRE(system_runner->run());
  }

  [[nodiscard]] std::vector<std::string> get_channels_matching(std::string_view pattern) const
  {
    const auto memory_resource = jewels::memory::MemoryResource{std::pmr::new_delete_resource()};
    const auto chunk_reader_factory =
      std::make_shared<clockwork_logging::offboard::ChunkReaderWriterFactory<>>(memory_resource);
    clockwork_logging::OffboardLogReader log_reader(
      output_log_path, {}, {}, clockwork_logging::DecompressOption::decompress, chunk_reader_factory);
    REQUIRE(log_reader.open({}));

    std::vector<std::string> matching;
    for (const auto& channel : log_reader.get_channels())
    {
      if (channel.find(pattern) != std::string::npos)
      {
        matching.push_back(channel);
      }
    }
    REQUIRE(log_reader.close());
    return matching;
  }

  [[nodiscard]] size_t count_messages(std::string_view channel_pattern) const
  {
    const auto memory_resource = jewels::memory::MemoryResource{std::pmr::new_delete_resource()};
    const auto chunk_reader_factory =
      std::make_shared<clockwork_logging::offboard::ChunkReaderWriterFactory<>>(memory_resource);
    clockwork_logging::OffboardLogReader log_reader(
      output_log_path, {}, {}, clockwork_logging::DecompressOption::decompress, chunk_reader_factory);
    REQUIRE(log_reader.open([channel_pattern](std::string_view channel)
                            { return channel.find(channel_pattern) != std::string_view::npos; }));
    size_t count = 0;
    while (log_reader.next_message())
    {
      ++count;
    }
    REQUIRE(log_reader.close());
    return count;
  }
};
} // namespace

TEST_CASE("Cog metrics report groups are published via system runner", "[cog_metrics][report_groups][csr]")
{
  using namespace std::chrono_literals;

  const auto memory_resource = jewels::memory::MemoryResource{std::pmr::new_delete_resource()};
  const auto chunk_reader_factory =
    std::make_shared<clockwork_logging::offboard::ChunkReaderWriterFactory<>>(memory_resource);

  SECTION("System runs successfully with cog metrics report groups")
  {
    auto config = ClockworkSystemRunnerConfig{
      .process_description_path = process_description_path(), .start_time = start_time, .end_time = end_time};
    auto system_runner = ClockworkSystemRunner::create(config);
    REQUIRE(system_runner);
    REQUIRE(system_runner->run());
  }

  SECTION("Default cog has event and telemetry metrics channels")
  {
    LoggedSystem sys;

    auto event_channels = sys.get_channels_matching("report-groups/CogMetricsDefaultCog/cog_event_metrics_group");
    auto telemetry_channels =
      sys.get_channels_matching("report-groups/CogMetricsDefaultCog/cog_telemetry_metrics_group");

    CHECK_FALSE(event_channels.empty());
    CHECK_FALSE(telemetry_channels.empty());
  }

  SECTION("Custom cog has event and telemetry metrics channels")
  {
    LoggedSystem sys;

    auto event_channels = sys.get_channels_matching("report-groups/CogMetricsCustomCog/cog_event_metrics_group");
    auto telemetry_channels =
      sys.get_channels_matching("report-groups/CogMetricsCustomCog/cog_telemetry_metrics_group");

    CHECK_FALSE(event_channels.empty());
    CHECK_FALSE(telemetry_channels.empty());
  }

  SECTION("Driver cog also has default cog metrics channels")
  {
    LoggedSystem sys;

    // The driver is a non-init cog and has no explicit disable policy,
    // so it also gets cog metrics report groups by default.
    auto event_channels = sys.get_channels_matching("report-groups/CogMetricsDriverCog/cog_event_metrics_group");
    auto telemetry_channels =
      sys.get_channels_matching("report-groups/CogMetricsDriverCog/cog_telemetry_metrics_group");

    CHECK_FALSE(event_channels.empty());
    CHECK_FALSE(telemetry_channels.empty());
  }

  SECTION("Default cog event metrics have correct batch size")
  {
    LoggedSystem sys;

    clockwork_logging::OffboardLogReader log_reader(
      sys.output_log_path, {}, {}, clockwork_logging::DecompressOption::decompress, chunk_reader_factory);
    REQUIRE(log_reader.open(
      [](std::string_view channel)
      {
        return channel.find("report-groups/CogMetricsDefaultCog/cog_event_metrics_group") != std::string_view::npos;
      }));

    using EventGroupTap = Tappy<cog_metrics_test::CogMetricsDefaultCog_cog_event_metrics_group>;
    std::vector<EventGroupTap> messages;
    while (auto logged_msg = log_reader.next_message())
    {
      auto& msg = messages.emplace_back();
      auto buf = std::as_writable_bytes(jewels::as_single_item_span(msg));
      std::ranges::copy(logged_msg->data, buf.begin());
    }
    REQUIRE(log_reader.close());

    // At 10Hz for 3 seconds = ~30 executions, batch_size=10 (default), so expect at least 2 batches
    REQUIRE(messages.size() >= 2);

    for (const auto& msg : messages)
    {
      // Default batch size is 10
      CHECK(msg.get_signals().size() == 10);

      // Execution interval must be positive
      CHECK(msg.get_execution_interval().count() > 0);
    }
  }

  SECTION("Custom cog event metrics have policy-specified batch size")
  {
    LoggedSystem sys;

    clockwork_logging::OffboardLogReader log_reader(
      sys.output_log_path, {}, {}, clockwork_logging::DecompressOption::decompress, chunk_reader_factory);
    REQUIRE(log_reader.open(
      [](std::string_view channel)
      { return channel.find("report-groups/CogMetricsCustomCog/cog_event_metrics_group") != std::string_view::npos; }));

    using EventGroupTap = Tappy<cog_metrics_test::CogMetricsCustomCog_cog_event_metrics_group>;
    std::vector<EventGroupTap> messages;
    while (auto logged_msg = log_reader.next_message())
    {
      auto& msg = messages.emplace_back();
      auto buf = std::as_writable_bytes(jewels::as_single_item_span(msg));
      std::ranges::copy(logged_msg->data, buf.begin());
    }
    REQUIRE(log_reader.close());

    // At 10Hz for 3 seconds = ~30 executions, batch_size=5 (from policy), so expect at least 4 batches
    REQUIRE(messages.size() >= 4);

    for (const auto& msg : messages)
    {
      // Policy-specified batch size is 5
      CHECK(msg.get_signals().size() == 5);
      CHECK(msg.get_execution_interval().count() > 0);

      for (const auto& signal : msg.get_signals())
      {
        // CustomCog only has new_trigger condition, which must be active for every execution
        CHECK(signal.get_new_trigger_active_value());

        // unseen_messages is the number of new messages received on this execution.
        // The driver publishes exactly 1 trigger per cycle.
        CHECK(signal.get_trigger_unseen_messages_value() == 1);

        // The cog publishes exactly one ack per execution
        CHECK(signal.get_ack_num_messages_value() == 1);

        // No messages should be dropped (channel capacity=10, consumed every cycle)
        CHECK(signal.get_trigger_dropped_messages_value() == 0);

        // Execution duration must be non-negative
        CHECK(signal.get_cog_exec_duration_value().count() >= 0);
      }
    }
  }

  SECTION("Default cog telemetry metrics contain valid aggregated data")
  {
    LoggedSystem sys;

    clockwork_logging::OffboardLogReader log_reader(
      sys.output_log_path, {}, {}, clockwork_logging::DecompressOption::decompress, chunk_reader_factory);
    REQUIRE(log_reader.open(
      [](std::string_view channel)
      {
        return channel.find("report-groups/CogMetricsDefaultCog/cog_telemetry_metrics_group") != std::string_view::npos;
      }));

    using TelemetryGroupTap = Tappy<cog_metrics_test::CogMetricsDefaultCog_cog_telemetry_metrics_group>;
    std::vector<TelemetryGroupTap> messages;
    while (auto logged_msg = log_reader.next_message())
    {
      auto& msg = messages.emplace_back();
      auto buf = std::as_writable_bytes(jewels::as_single_item_span(msg));
      std::ranges::copy(logged_msg->data, buf.begin());
    }
    REQUIRE(log_reader.close());

    // Default max_duration=1s, 3-second run, so expect at least 2 telemetry messages
    REQUIRE(messages.size() >= 2);

    for (const auto& msg : messages)
    {
      // Execution count must be positive
      CHECK(msg.get_execution_count() > 0);
      CHECK(msg.get_execution_interval().count() > 0);

      // Aggregated cog_exec_duration: min <= max must hold
      CHECK(msg.get_agg_cog_exec_duration_value_max() >= msg.get_agg_cog_exec_duration_value_min());

      // Mean duration should be non-negative
      CHECK(msg.get_agg_cog_exec_duration_value_mean() >= 0.0f);

      // Aggregated unseen messages: min <= max must hold
      CHECK(msg.get_agg_trigger_a_unseen_messages_value_max() >= msg.get_agg_trigger_a_unseen_messages_value_min());
      CHECK(msg.get_agg_trigger_b_unseen_messages_value_max() >= msg.get_agg_trigger_b_unseen_messages_value_min());

      // The cog publishes exactly 1 ack per execution, so min/max/mean should all equal 1
      CHECK(msg.get_agg_ack_num_messages_value_min() == 1);
      CHECK(msg.get_agg_ack_num_messages_value_max() == 1);
      CHECK(msg.get_agg_ack_num_messages_value_mean() == 1.0f);

      // Unseen messages backlog should stay below channel capacity (cog keeps up)
      CHECK(msg.get_agg_trigger_a_unseen_messages_value_max() < 10);
      CHECK(msg.get_agg_trigger_b_unseen_messages_value_max() < 10);
    }
  }

  SECTION("Default cog event metrics contain per-input and per-condition signals")
  {
    LoggedSystem sys;

    clockwork_logging::OffboardLogReader log_reader(
      sys.output_log_path, {}, {}, clockwork_logging::DecompressOption::decompress, chunk_reader_factory);
    REQUIRE(log_reader.open(
      [](std::string_view channel)
      {
        return channel.find("report-groups/CogMetricsDefaultCog/cog_event_metrics_group") != std::string_view::npos;
      }));

    using EventGroupTap = Tappy<cog_metrics_test::CogMetricsDefaultCog_cog_event_metrics_group>;

    size_t total_signals = 0;
    size_t new_trigger_a_active_count = 0;
    size_t total_trigger_a_unseen = 0;
    size_t total_trigger_b_unseen = 0;

    while (auto logged_msg = log_reader.next_message())
    {
      EventGroupTap msg;
      auto buf = std::as_writable_bytes(jewels::as_single_item_span(msg));
      std::ranges::copy(logged_msg->data, buf.begin());

      const auto& signals = msg.get_signals();
      REQUIRE(signals.size() > 0);

      for (const auto& signal : signals)
      {
        ++total_signals;

        // At least one execution condition must be active for the cog to have run
        CHECK((signal.get_new_trigger_a_active_value() || signal.get_periodic_100ms_active_value()));

        // cog_exec_duration should be non-negative
        CHECK(signal.get_cog_exec_duration_value().count() >= 0);

        // The cog implementation publishes exactly one ack per execution
        CHECK(signal.get_ack_num_messages_value() == 1);

        // No messages should be dropped (channel capacity=10, consumed every cycle)
        CHECK(signal.get_trigger_a_dropped_messages_value() == 0);
        CHECK(signal.get_trigger_b_dropped_messages_value() == 0);

        if (signal.get_new_trigger_a_active_value())
        {
          ++new_trigger_a_active_count;
        }

        // Accumulate unseen messages across all signals to validate totals
        total_trigger_a_unseen += signal.get_trigger_a_unseen_messages_value();
        total_trigger_b_unseen += signal.get_trigger_b_unseen_messages_value();
      }
    }
    REQUIRE(log_reader.close());
    REQUIRE(total_signals > 0);

    // Provide context on failure
    INFO("total_signals = " << total_signals);
    INFO("new_trigger_a_active_count = " << new_trigger_a_active_count);
    INFO("total_trigger_a_unseen = " << total_trigger_a_unseen);
    INFO("total_trigger_b_unseen = " << total_trigger_b_unseen);

    // The driver sends trigger_a every cycle, so new_trigger_a should be the
    // dominant execution condition (allow some tolerance for initial startup)
    CHECK(new_trigger_a_active_count > total_signals / 2);

    // unseen_messages is the number of new messages received on each execution.
    // The driver publishes 1 trigger_a and 1 trigger_b per cycle. Each execution
    // triggered by new_trigger_a sees exactly 1 new message. The periodic-only
    // execution(s) see 0 new messages since the driver hasn't published yet.
    CHECK(total_trigger_a_unseen == new_trigger_a_active_count);
    CHECK(total_trigger_b_unseen == new_trigger_a_active_count);
  }

  SECTION("Channel naming follows the report-groups convention")
  {
    LoggedSystem sys;

    auto all_report_group_channels = sys.get_channels_matching("/_clockwork/report-groups/");

    // All three cogs should have event and telemetry channels
    // (default_cog, custom_cog, driver_cog) × 2 groups = 6 channels
    CHECK(all_report_group_channels.size() >= 6);

    // Verify naming pattern: /_clockwork/report-groups/{CogName}/{group_name}/{uuid}
    for (const auto& channel : all_report_group_channels)
    {
      CHECK(channel.find("/_clockwork/report-groups/") != std::string::npos);
      const bool is_event = channel.find("cog_event_metrics_group") != std::string::npos;
      const bool is_telemetry = channel.find("cog_telemetry_metrics_group") != std::string::npos;
      // Every report-groups channel containing "cog_" should be either event or telemetry
      // (there may also be other report group channels if user-defined groups existed)
      if (channel.find("cog_") != std::string::npos)
      {
        CHECK((is_event || is_telemetry));
      }
    }
  }

  SECTION("SignalMetadataConfig persistent message includes cog metrics report groups")
  {
    LoggedSystem sys;

    // The signal metadata persistent tachyon is loaded from disk, not logged
    // as a channel.  Verify that the logged output contains report-group
    // channels for all three cogs as indirect evidence that the metadata was
    // generated and applied correctly.
    auto all_rg_channels = sys.get_channels_matching("/_clockwork/report-groups/");
    // 3 cogs × 2 groups (event + telemetry) = 6 channels
    CHECK(all_rg_channels.size() >= 6);
  }
}
} // namespace clockwork
