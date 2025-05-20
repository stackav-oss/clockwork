// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/examples/log_runner/test_message.hh"
#include "clockwork/logging/channel_type.hh"
#include "clockwork/logging/decompress_option.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/message_encoding.hh"
#include "clockwork/logging/offboard/types.hh"
#include "clockwork/logging/offboard/writer.hh"
#include "clockwork/logging/readers/offboard_log_reader.hh"
#include "clockwork/logging/readers/types.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/test_tools/clockwork_system_runner.hh"
#include "jewels/container/compare.hh"
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
  auto channel_status = writer.create_channel(clockwork_logging::offboard::LoggedChannelMetadata{
    .channel_name = "test_channel",
    .message_encoding = clockwork_logging::MessageEncoding::tachyon,
    .channel_type = clockwork_logging::ChannelType::regular,
  });

  if (!channel_status)
  {
    jewels::log_cerr_error("Error adding channel");
    return jewels::unexpected(jewels::MonoError{});
  }

  auto channel_status2 = writer.create_channel(clockwork_logging::offboard::LoggedChannelMetadata{
    .channel_name = "test_channel2",
    .message_encoding = clockwork_logging::MessageEncoding::tachyon,
    .channel_type = clockwork_logging::ChannelType::regular,
  });

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

    auto time =
      jewels::time::get_ns(start_time) + (std::chrono::duration_cast<std::chrono::nanoseconds>(10ms).count() * i);
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
  REQUIRE(log_reader.open([](auto channel_name) { return channel_name == "test_channel"; }));
  for (int i = 0; i < num_log_messages; ++i)
  {
    auto next_message = log_reader.next_message();
    REQUIRE(next_message);
    Tappy<clockwork::logging::test::TestMessage> test_message;
    auto test_message_buf = std::as_writable_bytes(jewels::as_single_item_span(test_message));
    std::ranges::copy(next_message->data, test_message_buf.begin());

    CHECK(test_message.get_message_string() == fmt::format("A test message index {}", i));
  }
}

TEST_CASE("Test Clockwork system runner")
{

  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto input_log_path = (test_dir.get_path() / input_log_directory_name).string();
  const auto output_log_path = (test_dir.get_path() / output_log_directory_name).string();

  const auto process_description_path = jewels::fix_clockwork_path(
    "clockwork/examples/log_runner/"
    "clockwork.clockwork.examples.log_runner.log_runner_example_system.log_runner_example_system.proc.tachyon");

  const auto log_reader_config_path =
    jewels::fix_clockwork_path("clockwork/examples/log_runner/"
                               "log_runner_example_system.log_runner_example_system.SimCpu_log_reader_config.tachyon");

  const auto log_writer_config_path = jewels::fix_clockwork_path(
    "clockwork/examples/log_runner/"
    "log_runner_example_system.log_runner_example_system.SimCpu_telemetry_logger_config.tachyon");

  REQUIRE(write_log(input_log_path));
  auto config = ClockworkSystemRunnerConfig{
    .process_description_path = process_description_path,
    .input_log_config = LogConfig(input_log_path, log_reader_config_path),
    .output_log_config = LogConfig(output_log_path, log_writer_config_path)};

  auto system_runner = ClockworkSystemRunner::create(config);
  REQUIRE(system_runner);
  REQUIRE(system_runner->run());

  validate_log(input_log_path);
}
} // namespace clockwork
