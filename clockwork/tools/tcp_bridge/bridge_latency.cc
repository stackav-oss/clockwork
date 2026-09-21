// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/readers/log_processor.hh"
#include "clockwork/logging/readers/types.hh"
#include "clockwork/pinion/bridge_status_clk_cc.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/serialization/cpp/tachyon_upgrader.hh"
#include "jewels/math/constants.hh"

#include <fmt/base.h>
#include <fmt/chrono.h> // IWYU pragma: keep
#include <tclap/CmdLine.h>
#include <tclap/MultiArg.h>
#include <tclap/UnlabeledValueArg.h>
#include <tclap/ValueArg.h>
#include <tclap/ValuesConstraint.h>
#include <wise_enum.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <ratio>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace clockwork::pinion
{

namespace
{

/// Latency counter types
WISE_ENUM_CLASS((LatencyType, uint8_t), recv, xfer, comprs, bridge, rates, hdrs)

/// Bridge status channel name
constexpr auto bridge_status_channel = "/tcp_bridge_status";

/// Bridge latency tool state
struct ToolState
{
  /// Host names to display
  std::unordered_set<std::string> host_name_set;

  /// Client channels to display
  std::unordered_set<std::string> client_channel_set;

  /// Server channels to display
  std::unordered_set<std::string> server_channel_set;

  /// Latency types to display
  std::unordered_set<LatencyType> latency_type_set;
};

/// Initialize the latency tool state
/// @param[in] host_names Host names to display
/// @param[in] client_channels Client channels to display
/// @param[in] server_channels Server channels to display
/// @param[in[ latency_types Latency types to display
void init_tool_state(
  const std::vector<std::string>& host_names,
  const std::vector<std::string>& client_channels,
  const std::vector<std::string>& server_channels,
  const std::vector<std::string>& latency_types,
  ToolState& tool_state)
{
  tool_state.host_name_set = std::unordered_set<std::string>(host_names.begin(), host_names.end());
  tool_state.client_channel_set = std::unordered_set<std::string>(client_channels.begin(), client_channels.end());
  tool_state.server_channel_set = std::unordered_set<std::string>(server_channels.begin(), server_channels.end());
  tool_state.latency_type_set.clear();
  if (latency_types.empty())
  {
    tool_state.latency_type_set.insert(LatencyType::bridge);
    tool_state.latency_type_set.insert(LatencyType::rates);
  }
  else
  {
    for (const auto& latency_type : latency_types)
    {
      if (latency_type == "all")
      {
        for (const auto latency_type_enum : wise_enum::range<LatencyType>)
        {
          tool_state.latency_type_set.insert(latency_type_enum.value);
        }
      }
      else
      {
        const auto& latency_enum = wise_enum::from_string<LatencyType>(latency_type);
        if (latency_enum.has_value())
        {
          tool_state.latency_type_set.insert(latency_enum.value());
        }
        else
        {
          std::cerr << "Ignoring unknown latency type: " << latency_type << '\n';
        }
      }
    }
  }
}

/// Print a line of rates output
/// @param[in] channel_name Channel name
/// @param[in] prefix Client or server prefix string
/// @param[in] latency_type Latency type (rates)
/// @param[in] message_rate_hz Message rate in hz
/// @param[in] compressed_data_rate_bps Compressed data rate in bytes/sec
void print_rates_line(
  std::string_view channel_name,
  std::string_view prefix,
  LatencyType latency_type,
  float message_rate_hz,
  float compressed_data_rate_bps)
{
  fmt::println(
    "  {} {} {:6s} msgs/sec: {:.3f} KB/sec: {:.3f}",
    channel_name,
    prefix,
    wise_enum::to_string(latency_type),
    message_rate_hz,
    compressed_data_rate_bps / jewels::math::constants::bytes_per_kb<float>);
}

/// Print a line of null headers output
/// @param[in] channel_name Channel name
/// @param[in] prefix Client or server prefix string
/// @param[in] latency_type Latency type (hdrs)
/// @param[in] null_header_rate_hz Rate of null headers sent to avoid lost tails
/// @param[in] keep_alive_rate_hz Rate of null headers sent as keep-alives
void print_null_headers_line(
  std::string_view channel_name,
  std::string_view prefix,
  LatencyType latency_type,
  float null_header_rate_hz,
  float keep_alive_rate_hz)
{
  fmt::println(
    "  {} {} {:6s} null-hdr: {:.3f}hz keep-alive: {:.3f}hz",
    channel_name,
    prefix,
    wise_enum::to_string(latency_type),
    null_header_rate_hz,
    keep_alive_rate_hz);
}

/// Print a line of latency output
/// @param[in] channel_name Channel name
/// @param[in] prefix Client or server prefix string
/// @param[in] latency_type Latency type
/// @param[in] mean_value Mean latency value
/// @param[in] max_value Max latency value
void print_latency_line(
  std::string_view channel_name,
  std::string_view prefix,
  LatencyType latency_type,
  std::chrono::nanoseconds mean_value,
  std::chrono::nanoseconds max_value)
{
  fmt::println(
    "  {} {} {:6s} mean: {:.3} max: {:.3}",
    channel_name,
    prefix,
    wise_enum::to_string(latency_type),
    std::chrono::duration<double, std::milli>(mean_value),
    std::chrono::duration<double, std::milli>(max_value));
}

/// Print latency counters
/// @param[in] counters Counters to print
/// @param[in] prefix Client/server prefix
/// @param[in] latency_types Latency types to print
void print_bridge_status_counters(
  const Tappy<BridgeClientServerCounters>& counters,
  std::string_view prefix,
  const std::unordered_set<LatencyType>& latency_types)
{
  fmt::println("");
  if (latency_types.contains(LatencyType::rates))
  {
    print_rates_line(
      counters.get_channel_name(),
      prefix,
      LatencyType::rates,
      counters.get_message_rate_hz(),
      counters.get_compressed_data_rate_bps());
  }
  if (latency_types.contains(LatencyType::hdrs))
  {
    print_null_headers_line(
      counters.get_channel_name(),
      prefix,
      LatencyType::hdrs,
      counters.get_null_header_rate_hz(),
      counters.get_keep_alive_rate_hz());
  }
  if (latency_types.contains(LatencyType::recv))
  {
    print_latency_line(
      counters.get_channel_name(),
      prefix,
      LatencyType::recv,
      counters.get_average_receive_latency(),
      counters.get_max_receive_latency());
  }
  if (latency_types.contains(LatencyType::xfer))
  {
    print_latency_line(
      counters.get_channel_name(),
      prefix,
      LatencyType::xfer,
      counters.get_average_transfer_time(),
      counters.get_max_transfer_time());
  }
  if (latency_types.contains(LatencyType::comprs))
  {
    print_latency_line(
      counters.get_channel_name(),
      prefix,
      LatencyType::comprs,
      counters.get_average_compression_time(),
      counters.get_max_compression_time());
  }
  if (latency_types.contains(LatencyType::bridge))
  {
    print_latency_line(
      counters.get_channel_name(),
      prefix,
      LatencyType::bridge,
      counters.get_average_bridge_latency(),
      counters.get_max_bridge_latency());
  }
}

/// Process a bridge status message
/// @param[in] publish_time Message publish time
/// @param[in] msg Bridge status message
/// @param[in] host_names Host names to process
/// @param[in] client_channels Client channels to print
/// @param[in] server_channels Server channels to print
/// @param[in] latency_types Latency types to print
void process_bridge_status_message(
  clockwork_logging::LogTimestamp publish_time,
  const Tappy<BridgeStatus>& msg,
  const std::unordered_set<std::string>& host_names,
  const std::unordered_set<std::string>& client_channels,
  const std::unordered_set<std::string>& server_channels,
  const std::unordered_set<LatencyType>& latency_types)
{
  if (!host_names.empty() && !host_names.contains(std::string{msg.get_host_name()}))
  {
    return;
  }
  const bool print_all = client_channels.empty() && server_channels.empty();
  fmt::println("{:.9f} {}", std::chrono::duration<double>(publish_time.get_duration()).count(), msg.get_host_name());
  if (print_all || !client_channels.empty())
  {
    for (const auto& counters : msg.get_client_counters())
    {
      if (!print_all && !client_channels.contains(std::string{counters.get_channel_name()}))
      {
        continue;
      }
      print_bridge_status_counters(counters, "client", latency_types);
    }
    fmt::println("");
  }
  if (print_all || !server_channels.empty())
  {
    for (const auto& counters : msg.get_server_counters())
    {
      if (!print_all && !server_channels.contains(std::string{counters.get_channel_name()}))
      {
        continue;
      }
      print_bridge_status_counters(counters, "server", latency_types);
    }
    fmt::println("");
  }
}

} // namespace

} // namespace clockwork::pinion

int main(int32_t argc, char* argv[])
{
  try
  {
    std::vector<std::string> latency_type_values;
    latency_type_values.reserve(wise_enum::enumerators<clockwork::pinion::LatencyType>::size + 1U);
    latency_type_values.emplace_back("all");
    for (const auto latency_type_enum : wise_enum::range<clockwork::pinion::LatencyType>)
    {
      latency_type_values.emplace_back(latency_type_enum.name);
    }
    TCLAP::ValuesConstraint<std::string> latency_type_constraint(latency_type_values);

    TCLAP::CmdLine cmd("Bridge latency tool", ' ', "1.0", true);
    const TCLAP::MultiArg<std::string> host_name_arg("n", "host-name", "Bridge host name", false, "host name", cmd);
    const TCLAP::MultiArg<std::string> client_channel_arg(
      "c", "client-channel", "Bridge client channel topic ", false, "channel name", cmd);
    const TCLAP::MultiArg<std::string> server_channel_arg(
      "s", "server-channel", "Bridge server channel topic ", false, "channel name", cmd);
    const TCLAP::ValueArg<int64_t> start_offset_arg(
      "o", "start-offset", "Start offset in seconds ", false, 0, "seconds", cmd);
    const TCLAP::MultiArg<std::string> latency_type_arg(
      "t", "latency-type", "Latency type", false, &latency_type_constraint, cmd);
    const TCLAP::UnlabeledValueArg<std::string> log_uri_arg("uri", "Log URI", true, "", "uri", cmd);

    cmd.parse(argc, argv);

    const auto& log_uri = log_uri_arg.getValue();
    const auto& start_offset = start_offset_arg.getValue();

    clockwork::pinion::ToolState tool_state{};
    clockwork::pinion::init_tool_state(
      host_name_arg.getValue(),
      client_channel_arg.getValue(),
      server_channel_arg.getValue(),
      latency_type_arg.getValue(),
      tool_state);

    clockwork_logging::LogProcessor reader(
      clockwork_logging::LogReaderConfig{
        .uri = log_uri,
        .interval = {},
        .relative_interval =
          clockwork_logging::RelativeInterval{
            .start_offset = std::chrono::seconds(start_offset),
            .end_offset = std::chrono::nanoseconds(std::numeric_limits<int64_t>::max()),
          },
        .topic_filter = {},
      });

    // NOLINTNEXTLINE(cert-err33-c) False positive
    reader.template add_tappy_callback<clockwork::Tappy<clockwork::pinion::BridgeStatus>>(
      clockwork::pinion::bridge_status_channel,
      [&tool_state](clockwork_logging::LogTimestamp publish_time, const auto& msg)
      {
        clockwork::pinion::process_bridge_status_message(
          publish_time,
          msg,
          tool_state.host_name_set,
          tool_state.client_channel_set,
          tool_state.server_channel_set,
          tool_state.latency_type_set);
      });

    fmt::println("");
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
