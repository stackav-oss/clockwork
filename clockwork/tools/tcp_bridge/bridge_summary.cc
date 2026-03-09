// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/readers/log_processor.hh"
#include "clockwork/logging/readers/types.hh"
#include "clockwork/pinion/bridge_status_clk_cc.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/serialization/cpp/tachyon_upgrader.hh"

#include <fmt/base.h>
#include <fmt/chrono.h> // IWYU pragma: keep
#include <tclap/CmdLine.h>
#include <tclap/SwitchArg.h>
#include <tclap/UnlabeledValueArg.h>
#include <tclap/ValueArg.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <ratio>
#include <span>
#include <string>
#include <utility>

namespace clockwork::pinion
{

namespace
{

/// Bridge status channel name
constexpr auto bridge_status_channel = "/tcp_bridge_status";

/// Client total channel name
constexpr auto client_total_channel_name = "CLIENT TOTAL";

/// Server total channel name
constexpr auto server_total_channel_name = "SERVER TOTAL";

/// Channel client/server summary counters
struct ClientServerChannelSummary
{
  /// Number of samples in the summary
  size_t sample_count{};

  /// Combined counters
  Tappy<BridgeClientServerCounters> counters{};
};

/// Channel client/server counter map
using ClientServerCounterMap = std::map<std::string, ClientServerChannelSummary>;

/// Per host counters
struct HostCounters
{
  /// Bridge client counters
  ClientServerCounterMap client_counters{};

  /// Bridge server counters
  ClientServerCounterMap server_counters{};
};

/// Host counter map
using HostCounterMap = std::map<std::string, HostCounters>;

/// Combine client/server counters into a summary
/// @param[in] counters Counters to combine
/// @param[in,out] summary Summary counters
void combine_bridge_counters(const Tappy<BridgeClientServerCounters>& counters, ClientServerChannelSummary& summary)
{
  if (counters.get_message_rate_hz() != 0.0f)
  {
    ++summary.sample_count;
    summary.counters.set_message_rate_hz(summary.counters.get_message_rate_hz() + counters.get_message_rate_hz());
    summary.counters.set_data_rate_bps(summary.counters.get_data_rate_bps() + counters.get_data_rate_bps());
    summary.counters.set_compressed_data_rate_bps(
      summary.counters.get_compressed_data_rate_bps() + counters.get_compressed_data_rate_bps());
    summary.counters.set_average_receive_latency(
      summary.counters.get_average_receive_latency() + counters.get_average_receive_latency());
    summary.counters.set_max_receive_latency(
      std::max(summary.counters.get_max_receive_latency(), counters.get_max_receive_latency()));
    summary.counters.set_average_transfer_time(
      summary.counters.get_average_transfer_time() + counters.get_average_transfer_time());
    summary.counters.set_max_transfer_time(
      std::max(summary.counters.get_max_transfer_time(), counters.get_max_transfer_time()));
    summary.counters.set_average_compression_time(
      summary.counters.get_average_compression_time() + counters.get_average_compression_time());
    summary.counters.set_max_compression_time(
      std::max(summary.counters.get_max_compression_time(), counters.get_max_compression_time()));
    summary.counters.set_average_bridge_latency(
      summary.counters.get_average_bridge_latency() + counters.get_average_bridge_latency());
    summary.counters.set_max_bridge_latency(
      std::max(summary.counters.get_max_bridge_latency(), counters.get_max_bridge_latency()));
  }
}

/// Process a bridge status message
/// @param[in] msg Bridge status message
/// @param[in,out] counter_map Per host bridge status counters
void process_bridge_status_message(const Tappy<BridgeStatus>& msg, HostCounterMap& counter_map)
{
  const std::string host_name{msg.get_host_name()};
  auto& host_counters = counter_map[host_name];
  for (const auto& counters : msg.get_client_counters())
  {
    const std::string channel_name{counters.get_channel_name()};
    {
      auto& channel_summary = host_counters.client_counters[channel_name];
      combine_bridge_counters(counters, channel_summary);
    }
  }
  for (const auto& counters : msg.get_server_counters())
  {
    const std::string channel_name{counters.get_channel_name()};
    {
      auto& channel_summary = host_counters.server_counters[channel_name];
      combine_bridge_counters(counters, channel_summary);
    }
  }
}

/// Print the channel client/server summary map
/// @param[in] summary_map Channel client/server summary map
/// @param[in] verbose Flag to produce verbose output
void print_channel_summary_map(const ClientServerCounterMap& summary_map, bool verbose)
{
  for (const auto& [channel_name, summary] : summary_map)
  {
    if (!verbose && channel_name != client_total_channel_name && channel_name != server_total_channel_name)
    {
      continue;
    }
    fmt::println("");
    if (summary.sample_count == 0U)
    {
      fmt::println("    {}: NO MESSAGES", channel_name);
      continue;
    }
    const auto real_count = static_cast<float>(summary.sample_count);
    fmt::println("    {}:", channel_name);
    fmt::println("      message_rate: {:.3f}hz", summary.counters.get_message_rate_hz() / real_count);
    fmt::println("      data_rate_bps: {:.3f}bps", summary.counters.get_data_rate_bps() / real_count);
    fmt::println("      compressed_data_rate: {:.3f}bps", summary.counters.get_compressed_data_rate_bps() / real_count);
    fmt::println(
      "      compression_ratio: {:.3f}",
      summary.counters.get_data_rate_bps() / summary.counters.get_compressed_data_rate_bps());
    fmt::println(
      "      average_receive_latency: {:.3}",
      std::chrono::duration<float, std::milli>(summary.counters.get_average_receive_latency()) / real_count);
    fmt::println(
      "      max_receive_latency: {:.3}",
      std::chrono::duration<float, std::milli>(summary.counters.get_max_receive_latency()));
    fmt::println(
      "      average_transfer_time: {:.3}",
      std::chrono::duration<float, std::milli>(summary.counters.get_average_transfer_time()) / real_count);
    fmt::println(
      "      max_transfer_time: {:.3}",
      std::chrono::duration<float, std::milli>(summary.counters.get_max_transfer_time()));
    fmt::println(
      "      average_compression_time: {:.3}",
      std::chrono::duration<float, std::milli>(summary.counters.get_average_compression_time()) / real_count);
    fmt::println(
      "      max_compression_time: {:.3}",
      std::chrono::duration<float, std::milli>(summary.counters.get_max_compression_time()));
    fmt::println(
      "      average_bridge_latency: {:.3}",
      std::chrono::duration<float, std::milli>(summary.counters.get_average_bridge_latency()) / real_count);
    fmt::println(
      "      max_bridge_latency: {:.3}",
      std::chrono::duration<float, std::milli>(summary.counters.get_max_bridge_latency()));
  }
}

/// Print the bridge summary
/// @param[in] counter_map Bridge status summary counter map
/// @param[in] verbose Flag to produce verbose output
void print_bridge_summary(HostCounterMap& counter_map, bool verbose)
{
  for (const auto& [host_name, host_counters] : counter_map)
  {
    fmt::println("");
    fmt::println("{}:", host_name);
    fmt::println("");
    fmt::println("  Client counters:");
    print_channel_summary_map(host_counters.client_counters, verbose);
    fmt::println("");
    fmt::println("  Server counters:");
    print_channel_summary_map(host_counters.server_counters, verbose);
  }
  fmt::println("");
}

} // namespace

} // namespace clockwork::pinion

int main(int32_t argc, char* argv[])
{
  try
  {
    TCLAP::CmdLine cmd("Bridge status summary tool", ' ', "1.0", true);
    const TCLAP::ValueArg<int64_t> start_offset_arg(
      "o", "start-offset", "Start offset in seconds ", false, 0, "seconds", cmd);
    const TCLAP::SwitchArg verbose_arg("v", "verbose", "Verbose output", cmd);
    const TCLAP::UnlabeledValueArg<std::string> log_uri_arg("uri", "Log URI", true, "", "uri", cmd);

    cmd.parse(argc, argv);

    const auto& log_uri = log_uri_arg.getValue();
    const auto start_offset = start_offset_arg.getValue();
    const auto verbose = verbose_arg.getValue();

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

    clockwork::pinion::HostCounterMap counter_map{};

    // NOLINTNEXTLINE(cert-err33-c) False positive
    reader.template add_tappy_callback<clockwork::Tappy<clockwork::pinion::BridgeStatus>>(
      clockwork::pinion::bridge_status_channel,
      [&counter_map](clockwork_logging::LogTimestamp, const auto& msg)
      { clockwork::pinion::process_bridge_status_message(msg, counter_map); });

    if (!reader.process())
    {
      std::cerr << "Failed to process log\n";
      return 1;
    }
    clockwork::pinion::print_bridge_summary(counter_map, verbose);

    return 0;
  }
  catch (const std::exception& exc)
  {
    std::cerr << "Caught unexpected exception: " << exc.what() << '\n';
    return 1;
  }
}
