// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/examples/log_runner/test_message.hh"
#include "clockwork/logging/decompress_option.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_playback/end_of_log.hh"
#include "clockwork/logging/log_playback/log_message_fetcher.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/nolint_helper.hh"
#include "clockwork/logging/offboard/types.hh"
#include "clockwork/logging/offboard/writer.hh"
#include "clockwork/logging/readers/offboard_log_reader.hh"
#include "clockwork/logging/readers/types.hh"
#include "clockwork/logging/writers/deterministic_log_writer.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/test_tools/clockwork_system_runner.hh"
#include "clockwork/tools/metrics_channel_metadata/metrics_channel_metadata_config.hh"
#include "jewels/container/compare.hh"
#include "jewels/container/tap/var_array.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/std/span.hh"
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/time/conversions.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/utility/fix_clockwork_path.hh"

#include <catch2/catch_test_macros.hpp>
#include <fmt10/format.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>

/// Log directory name
constexpr auto input_log_directory_name = "input_log";
constexpr auto output_log_directory_name = "output_log";
constexpr int num_log_messages = 100;
namespace clockwork
{

jewels::expected<void, jewels::MonoError> write_log(std::string_view log_directory)
{
  using namespace std::chrono_literals;
  const auto memory_resource = jewels::memory::MemoryResource(std::pmr::new_delete_resource());

  clockwork_logging::offboard::Writer writer(memory_resource);

  const auto start_time = jewels::time::SyncTime(std::chrono::hours(1));
  if (!writer.open(log_directory))
  {
    jewels::log_cerr_error("error opening logger");
  }
  auto channel_status = writer.create_channel<Tappy<clockwork::logging::test::TestMessage>>("test_channel");

  if (!channel_status)
  {
    jewels::log_cerr_error("Error adding channel");
    return jewels::unexpected(jewels::MonoError{});
  }

  auto channel_status2 = writer.create_channel<Tappy<clockwork::logging::test::TestMessage>>("test_channel2");

  if (!channel_status2)
  {
    jewels::log_cerr_error("Error adding channel");
    return jewels::unexpected(jewels::MonoError{});
  }

  for (int i = 0; i < num_log_messages; ++i)
  {
    Tappy<clockwork::logging::test::TestMessage> test_message{};
    test_message.get_underlying_message_string().set_truncate(fmt::format("A test message index {}", i));

    const auto log_data = as_bytes(jewels::as_single_item_span(test_message));
    // We need to ensure our synthetic data from the input log is all published before the publisher cog in the system
    // starts publishing its test message.
    auto time =
      jewels::time::get_ns(start_time) + (std::chrono::duration_cast<std::chrono::nanoseconds>(1ms).count() * i);
    auto logged_message = clockwork_logging::offboard::LoggedMessage{
      .channel_name = "test_channel",
      .log_time = clockwork_logging::LogTimestamp{time},
      .transmit_time = clockwork_logging::LogTimestamp{time},
      .data = log_data};
    auto logged_message2 = clockwork_logging::offboard::LoggedMessage{
      .channel_name = "test_channel2",
      .log_time = clockwork_logging::LogTimestamp{time},
      .transmit_time = clockwork_logging::LogTimestamp{time},
      .data = log_data};

    auto writer_status = writer.write(logged_message);
    if (!writer_status)
    {
      jewels::log_cerr_error("Error writing to log");
      return jewels::unexpected(jewels::MonoError{});
    }
    auto writer_status2 = writer.write(logged_message2);
    if (!writer_status2)
    {
      jewels::log_cerr_error("Error writing to log");
      return jewels::unexpected(jewels::MonoError{});
    }

    jewels::log_cerr_info("log written success");
  }
  if (auto close_result = writer.close(); !close_result)
  {
    jewels::log_cerr_error("Error closing log: {}", close_result.error());
    return jewels::unexpected(jewels::MonoError{});
  }
  return {};
}

void validate_log(std::string_view log_uri)
{

  clockwork_logging::OffboardLogReader log_reader(log_uri, {}, {}, clockwork_logging::DecompressOption::decompress);
  REQUIRE(log_reader.open([](auto channel_name) { return channel_name == "Chan2" || channel_name == "Chan3"; }));
  clockwork_logging::LogTimestamp last_real_publish_time;
  for (int i = 0; i < num_log_messages; ++i)
  {
    auto next_message = log_reader.next_message();
    REQUIRE(next_message);
    Tappy<clockwork::logging::test::TestMessage> test_message;
    auto test_message_buf = std::as_writable_bytes(jewels::as_single_item_span(test_message));
    std::ranges::copy(next_message->data, test_message_buf.begin());

    CHECK(test_message.get_message_string() == fmt::format("A test message index {}", i));
    last_real_publish_time = next_message->publish_time;
  }
  REQUIRE(log_reader.close());

  // Validate end_of_log message is present. Use a separate log reader because the eol message is emitted with the same
  // timestamp as the last real message so the eol may or may not end up in the log file before the true last message.
  // However, in a non-test scenario the eol message doesn't get logged.
  clockwork_logging::OffboardLogReader end_of_log_reader(
    log_uri, {}, {}, clockwork_logging::DecompressOption::decompress);
  REQUIRE(end_of_log_reader.open([](auto channel_name)
                                 { return channel_name == clockwork_logging::end_of_log_channel_name; }));
  auto end_of_log_message = end_of_log_reader.next_message();
  REQUIRE(end_of_log_message);
  REQUIRE(end_of_log_message->topic == clockwork_logging::end_of_log_channel_name);
  REQUIRE(end_of_log_message->publish_time == last_real_publish_time);
  Tappy<clockwork_logging::EndOfLog> end_of_log_tappy;
  auto end_of_log_buf = std::as_writable_bytes(jewels::as_single_item_span(end_of_log_tappy));
  std::ranges::copy(end_of_log_message->data, end_of_log_buf.begin());
  CHECK(end_of_log_tappy.get_end_of_log() == true);
  REQUIRE(end_of_log_reader.close());

  clockwork_logging::OffboardLogReader metrics_metadata_log_reader(
    log_uri, {}, {}, clockwork_logging::DecompressOption::decompress);
  REQUIRE(metrics_metadata_log_reader.open(
    [](auto channel_name) { return channel_name == clockwork_logging::metrics_channel_metadata_channel_name; }));
  auto logged_metrics_metadata_report = metrics_metadata_log_reader.next_message();
  REQUIRE(logged_metrics_metadata_report);
  REQUIRE(logged_metrics_metadata_report->topic == clockwork_logging::metrics_channel_metadata_channel_name);
  auto metrics_channel_metadata =
    clockwork_logging::nolint_helper::byte_span_to_value_ptr<clockwork::tools::MetricsChannelMetadataReportTap>(
      logged_metrics_metadata_report->data);
  REQUIRE(metrics_channel_metadata);
  const auto* metrics_channel_metadata_ptr = metrics_channel_metadata.value();
  // Two cogs two metrics channels on each
  REQUIRE(metrics_channel_metadata_ptr->get_underlying_metrics_channels().size() == 4U);
}

TEST_CASE("Test Clockwork system runner")
{
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto input_log_path = (test_dir.get_path() / input_log_directory_name).string();
  const auto output_log_path = (test_dir.get_path() / output_log_directory_name).string();

  const auto process_description_path = jewels::fix_clockwork_path(
    "clockwork/examples/log_runner/"
    "clockwork.clockwork.examples.log_runner.log_runner_example_system.log_runner_example_system.proc.tachyon");

  const auto channel_publisher_config_path = jewels::fix_clockwork_path(
    "clockwork/examples/log_runner/"
    "log_runner_example_system.log_runner_example_system.SimCpu_channel_publisher_config.tachyon");

  const auto log_writer_config_path = jewels::fix_clockwork_path(
    "clockwork/examples/log_runner/"
    "log_runner_example_system.log_runner_example_system.SimCpu_telemetry_logger_config.tachyon");

  const auto metrics_channel_metadata_config_path = jewels::fix_clockwork_path(
    "clockwork/examples/log_runner/"
    "log_runner_example_system.log_runner_example_system.SimCpu_metrics_channel_metadata_config.tachyon");
  REQUIRE(write_log(input_log_path));

  auto config = ClockworkSystemRunnerConfig{
    .process_description_path = process_description_path,
    .metrics_channel_metadata_config_path = metrics_channel_metadata_config_path,
    .input_log_config = LogConfig(input_log_path, channel_publisher_config_path),
    .output_log_config = LogConfig(output_log_path, log_writer_config_path)};

  auto system_runner = ClockworkSystemRunner::create(config);
  REQUIRE(system_runner);
  REQUIRE(system_runner->run());

  validate_log(output_log_path);
}
} // namespace clockwork
