// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/exec_tools.hh"
#include "clockwork/logging/channel_publisher_config_clk_cc.hh"
#include "clockwork/logging/channel_type_clk_cc.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/log_writer_config_clk_cc.hh"
#include "clockwork/logging/message_encoding_clk_cc.hh"
#include "clockwork/logging/offboard/types.hh"
#include "clockwork/logging/offboard/writer.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/scaffolding/deterministic_logging_config.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/container/compare.hh"
#include "jewels/container/tap/var_array.hh"
#include "jewels/container/tap/var_string.hh"
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
#include <string_view>
#include <sys/types.h>
#include <unistd.h>

namespace clockwork
{
TEST_CASE("Read deterministic logging config from files")
{
  const jewels::testing::TmpDirectoryGuard tmpdir;
  const auto publisher_config_path = tmpdir.get_path() / "publisher_config";
  const auto writer_config_path = tmpdir.get_path() / "writer_config";

  constexpr auto publisher_channel_name = "publisher_channel";
  constexpr auto writer_channel_name = "writer_channel";

  const std::shared_ptr<Tappy<clockwork_logging::ChannelPublisherConfig<>>> channel_publisher_config =
    std::make_shared<Tappy<clockwork_logging::ChannelPublisherConfig<>>>();
  const std::shared_ptr<Tappy<clockwork_logging::LogWriterConfig<>>> log_writer_config =
    std::make_shared<Tappy<clockwork_logging::LogWriterConfig<>>>();

  channel_publisher_config->get_underlying_channels().resize(1);
  channel_publisher_config->get_underlying_channels().at(0).get_underlying_channel_name().set_truncate(
    publisher_channel_name);

  log_writer_config->get_underlying_channels().resize(1);
  log_writer_config->get_underlying_channels().at(0).get_underlying_channel_name().set_truncate(writer_channel_name);

  const auto publisher_bytes = std::as_bytes(jewels::as_single_item_span(*channel_publisher_config));
  const jewels::filesystem::File publisher_file{publisher_config_path, O_CREAT | O_WRONLY};
  REQUIRE(
    ::write(publisher_file.descriptor(), publisher_bytes.data(), publisher_bytes.size()) ==
    static_cast<ssize_t>(publisher_bytes.size()));

  const auto writer_bytes = std::as_bytes(jewels::as_single_item_span(*log_writer_config));
  const jewels::filesystem::File writer_file{writer_config_path, O_CREAT | O_WRONLY};
  REQUIRE(
    ::write(writer_file.descriptor(), writer_bytes.data(), writer_bytes.size()) ==
    static_cast<ssize_t>(writer_bytes.size()));

  SECTION("Both publisher and writer specified")
  {
    auto params = ExecutionParams{
      .execution_mode = ExecutionMode::deterministic,
      .channel_publisher_config_path = std::string{publisher_config_path.c_str()},
      .log_writer_config_path = std::string{writer_config_path.c_str()}};

    DeterministicLoggingConfig logging_config;
    REQUIRE(jewels::ok(get_deterministic_logging_config(jewels::Out{logging_config}, params)));
    REQUIRE(
      logging_config.channel_publisher_config->get_channels().begin()->get_channel_name() == publisher_channel_name);
    REQUIRE(logging_config.log_writer_config->get_channels().begin()->get_channel_name() == writer_channel_name);
  }
  SECTION("Publisher specified, not writer")
  {
    auto params = ExecutionParams{
      .execution_mode = ExecutionMode::deterministic,
      .channel_publisher_config_path = std::string{publisher_config_path.c_str()}};

    DeterministicLoggingConfig logging_config;
    REQUIRE(jewels::ok(get_deterministic_logging_config(jewels::Out{logging_config}, params)));
    REQUIRE(
      logging_config.channel_publisher_config->get_channels().begin()->get_channel_name() == publisher_channel_name);
    REQUIRE(!logging_config.log_writer_config);
  }

  SECTION("Writer invalid path")
  {
    auto params = ExecutionParams{
      .execution_mode = ExecutionMode::deterministic,
      .channel_publisher_config_path = std::string{publisher_config_path.c_str()},
      .log_writer_config_path = "/dsajf"};

    DeterministicLoggingConfig logging_config;
    REQUIRE(jewels::fails(get_deterministic_logging_config(jewels::Out{logging_config}, params)));
  }

  SECTION("Using online runner")
  {
    auto params = ExecutionParams{
      .execution_mode = ExecutionMode::online,
      .channel_publisher_config_path = std::string{publisher_config_path.c_str()},
      .log_writer_config_path = std::string{writer_config_path.c_str()}};

    DeterministicLoggingConfig logging_config;
    REQUIRE(jewels::ok(get_deterministic_logging_config(jewels::Out{logging_config}, params)));
    REQUIRE(!logging_config.channel_publisher_config);
    REQUIRE(!logging_config.log_writer_config);
  }
}

TEST_CASE("populate_start_and_end_times")
{
  TCLAP::CmdLine cmd("testing", ' ', "1.0", true);
  const ExecutionArgs execution_args{cmd};

  constexpr int64_t start_time_ns = 100000000000;
  constexpr int64_t end_time_ns = 200000000000;
  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};

  const jewels::testing::TmpDirectoryGuard test_dir;

  /// Log directory name
  constexpr auto log_directory_name = "test_log";

  /// Telemetry writer name
  constexpr auto writer_name = "test_writer";

  constexpr auto channel_name = "test_channel";

  const auto expected_log_path = (test_dir.get_path() / log_directory_name / "telemetry" / writer_name).string();

  clockwork_logging::offboard::Writer writer{memres};
  REQUIRE(writer.open(expected_log_path));

  REQUIRE(writer.create_channel(
    clockwork_logging::offboard::LoggedChannelMetadata{
      .channel_name = channel_name,
      .message_encoding = clockwork_logging::MessageEncoding::tachyon,
      .channel_type = clockwork_logging::ChannelType::regular,
    }));

  // Write a log file with the first message being our start time and last with the end time.
  auto logged_message = clockwork_logging::offboard::LoggedMessage{
    .channel_name = channel_name,
    .log_time = clockwork_logging::LogTimestamp{start_time_ns},
    .transmit_time = clockwork_logging::LogTimestamp{start_time_ns}};
  REQUIRE(writer.write(logged_message));
  logged_message.log_time = clockwork_logging::LogTimestamp{end_time_ns};
  logged_message.transmit_time = clockwork_logging::LogTimestamp{end_time_ns};
  REQUIRE(writer.write(logged_message));
  REQUIRE(writer.close());

  std::array<const char*, 6> args = {
    {"clockwork.bin",
     "--deterministic-runner",
     "--input-log-uri",
     expected_log_path.c_str(),
     "--channel-publisher-config",
     "some_path/another"}};

  cmd.parse(args.size(), args.data());
  auto execution_params = execution_args.make_execution_params();
  REQUIRE(execution_params);
  CHECK(execution_params->execution_mode == ExecutionMode::deterministic);
  CHECK(execution_params->start_time == std::nullopt);
  CHECK(execution_params->end_time == std::nullopt);
  auto times = calc_start_and_end_times(*execution_params);
  REQUIRE(times);
  CHECK(times->start == jewels::time::SyncTime(std::chrono::nanoseconds(start_time_ns)));
  CHECK(times->end == jewels::time::SyncTime(std::chrono::nanoseconds(end_time_ns)));
}
} // namespace clockwork
