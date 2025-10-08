// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/readers/log_processor.hh"
#include "clockwork/logging/readers/types.hh"
#include "clockwork/pinion/bridge_status.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/serialization/cpp/tachyon_upgrader.hh"

#include <fmt10/base.h>
#include <fmt10/chrono.h> // IWYU pragma: keep
#include <tclap/CmdLine.h>
#include <tclap/UnlabeledValueArg.h>

#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace clockwork::pinion
{

namespace
{

/// Bridge status channel name
constexpr auto bridge_status_channel = "/tcp_bridge_status";

/// Number of nanoseconds per second
constexpr auto nanoseconds_per_second = std::chrono::nanoseconds{std::chrono::seconds{1}}.count();

/// Print the diagnostics counters from a bridge status message
/// @param[in] msg Message
void print_diagnostics_counters(
  clockwork_logging::LogTimestamp publish_time, const Tappy<clockwork::pinion::BridgeStatus>& msg)
{
  const auto& diag_counters = msg.get_diagnostics_counters();
  fmt::println(
    "\n{} {:d}.{:09d}",
    msg.get_host_name(),
    publish_time.get_nanoseconds() / nanoseconds_per_second,
    publish_time.get_nanoseconds() % nanoseconds_per_second);
  fmt::println("    drop_count:               {}", diag_counters.get_drop_count());
  fmt::println("    failed_sends:             {}", diag_counters.get_failed_sends());
  fmt::println("    closed_socket_count:      {}", diag_counters.get_closed_socket_count());
  fmt::println("    failed_recvs:             {}", diag_counters.get_failed_recvs());
  fmt::println("    failed_reservations:      {}", diag_counters.get_failed_reservations());
  fmt::println("    malformed_messages:       {}", diag_counters.get_malformed_messages());
  fmt::println("    failed_commits:           {}", diag_counters.get_failed_commits());
  fmt::println("    failed_discards:          {}", diag_counters.get_failed_discards());
  fmt::println("    client_socket_errors:     {}", diag_counters.get_client_socket_errors());
  fmt::println("    progress_errors:          {}", diag_counters.get_progress_errors());
  fmt::println("    epoll_errors:             {}", diag_counters.get_epoll_errors());
  fmt::println("    status_errors:            {}", diag_counters.get_status_errors());
  fmt::println("    max_bridge_latency:       {}", diag_counters.get_max_bridge_latency().count());
  fmt::println("    max_latency_channel_name: {}", diag_counters.get_max_latency_channel_name());
}

} // namespace

} // namespace clockwork::pinion

int main(int32_t argc, char* argv[])
{
  try
  {
    TCLAP::CmdLine cmd("Dump TCP bridge diagnostics counters", ' ', "1.0", true);
    const TCLAP::UnlabeledValueArg<std::string> log_uri_arg("uri", "Log URI", true, "", "uri", cmd);

    cmd.parse(argc, argv);

    const auto& log_uri = log_uri_arg.getValue();

    clockwork_logging::LogProcessor reader(
      clockwork_logging::LogReaderConfig{
        .uri = log_uri,
        .interval = {},
        .relative_interval = {},
        .topic_filter = {},
      });

    // NOLINTNEXTLINE(cert-err33-c) False positive
    reader.template add_tappy_callback<clockwork::Tappy<clockwork::pinion::BridgeStatus>>(
      clockwork::pinion::bridge_status_channel,
      [](clockwork_logging::LogTimestamp publish_time, const auto& msg)
      { clockwork::pinion::print_diagnostics_counters(publish_time, msg); });

    if (!reader.process())
    {
      std::cerr << "Failed to process log\n";
      return 1;
    }

    return 0;
  }
  catch (const std::exception& exc)
  {
    std::cerr << "Caught unexpected exception: " << exc.what() << '\n';
    return 1;
  }
}
