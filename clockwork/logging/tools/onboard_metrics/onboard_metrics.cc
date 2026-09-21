// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/decompress_option.hh"
#include "clockwork/logging/lite_compressor.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/readers/abstract_log_reader.hh"
#include "clockwork/logging/readers/log_reader_factory.hh"
#include "clockwork/logging/readers/types.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/math/constants.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"

#include <fmt/base.h>
#include <fmt/format.h>
#include <tclap/CmdLine.h>
#include <tclap/MultiArg.h>
#include <tclap/UnlabeledValueArg.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <memory_resource>
#include <numeric>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace clockwork_logging::onboard
{

/// Onboard channel metrics
struct OnboardMetrics
{
  /// Number of messages
  size_t msgs{};

  /// Number of uncompresed message bytes
  size_t bytes{};

  /// Number of compressed message bytes
  size_t compressed_bytes{};
};

/// Format a human readable value
/// @param[in] value Value to print
/// @param[in] units_suffix Suffix to apply to printout
/// @return Human readable value string
std::string human_readable_value(double value, std::string_view units_suffix)
{
  if (value > jewels::math::constants::bytes_per_gb<double>)
  {
    return fmt::format("{:.3f} G{}", value / jewels::math::constants::bytes_per_gb<double>, units_suffix);
  }
  if (value > jewels::math::constants::bytes_per_mb<double>)
  {
    return fmt::format("{:.3f} M{}", value / jewels::math::constants::bytes_per_mb<double>, units_suffix);
  }
  if (value > jewels::math::constants::bytes_per_kb<double>)
  {
    return fmt::format("{:.3f} K{}", value / jewels::math::constants::bytes_per_kb<double>, units_suffix);
  }
  return fmt::format("{:.3f} {}", value, units_suffix);
}

/// Format a human readable value
/// @param[in] value Value to print
/// @param[in] units_suffix Suffix to apply to printout
/// @return Human readable value string
std::string human_readable_value(uint64_t value, std::string_view units_suffix)
{
  if (value < jewels::math::constants::bytes_per_kb<uint64_t>)
  {
    return fmt::format("{} {}", value, units_suffix);
  }
  return human_readable_value(static_cast<double>(value), units_suffix);
}

/// Open the source log reader
/// @param[in] memory_resource Memory resource
/// @param[in] log_uri Log URI
/// @param[in] maybe_desired_channels Optional set of desired channels
/// @return Reader pointer or LogError on failure
[[nodiscard]] LogExpected<std::unique_ptr<AbstractLogReader>> open_reader(
  jewels::memory::MemoryResource memory_resource,
  std::string_view log_uri,
  const std::optional<std::pmr::unordered_set<std::pmr::string>>& maybe_desired_channels)
{
  const auto channel_filter = [memory_resource, maybe_desired_channels](std::string_view channel)
  { return !maybe_desired_channels || maybe_desired_channels->contains(std::pmr::string{channel, memory_resource}); };
  try
  {
    auto reader_ptr = make_reader(log_uri, {}, {}, DecompressOption::dont_decompress);
    if (const auto open_result = reader_ptr->open(channel_filter); !open_result)
    {
      return jewels::unexpected(open_result.error());
    }
    return {std::move(reader_ptr)};
  }
  catch (const std::invalid_argument& exc)
  {
    jewels::log_cerr_error("{}", exc.what());
    return jewels::unexpected(LogError::failed_to_open_log_file);
  }
}

/// Dump onboard metrics for a log
/// @param[in] memory_resource Memory resource
/// @param[in] log_uri Source log URI
/// @param[in] maybe_desired_channels Optional set of desired channels
/// @return LogError on failure
[[nodiscard]] LogExpected<void> onboard_metrics(
  jewels::memory::MemoryResource memory_resource,
  std::string_view log_uri,
  const std::optional<std::pmr::unordered_set<std::pmr::string>>& maybe_desired_channels)
{
  const auto reader_result = open_reader(memory_resource, log_uri, maybe_desired_channels);
  if (!reader_result)
  {
    jewels::log_cerr_error("Failed to open {} for read: {}", log_uri, reader_result.error());
    return jewels::unexpected(reader_result.error());
  }
  auto& reader = *reader_result.value();
  LiteCompressor compressor{memory_resource};
  std::map<std::string, OnboardMetrics> metrics_map;
  std::optional<LogTimestamp> maybe_start_time;
  std::optional<LogTimestamp> maybe_end_time;
  while (true)
  {
    const auto maybe_message = reader.zero_copy_next_message();
    if (!maybe_message)
    {
      break;
    }
    if (!maybe_start_time)
    {
      maybe_start_time = maybe_message->publish_time;
    }
    maybe_end_time = maybe_message->publish_time;
    size_t data_size = 0U;
    size_t compressed_size = 0U;
    if (maybe_message->is_lite_compressed)
    {
      compressed_size = std::accumulate(
        maybe_message->data.begin(),
        maybe_message->data.end(),
        size_t{0U},
        [](size_t lhs, auto& rhs) { return lhs + rhs.size(); });
      const auto size_result = LiteCompressor::get_decompressed_size(maybe_message->data);
      if (!size_result)
      {
        jewels::log_cerr_error("Failed to decompress message on {}", maybe_message->topic);
        continue;
      }
      data_size = size_result.value();
    }
    else
    {
      const auto data_view = maybe_message->data | std::views::join;
      std::vector<std::byte> data(data_view.begin(), data_view.end());
      data_size = data.size();
      const auto compressed_data = compressor.compress(data);
      compressed_size = std::accumulate(
        compressed_data.begin(),
        compressed_data.end(),
        size_t{0U},
        [](size_t lhs, auto& rhs) { return lhs + rhs.size(); });
    }
    auto& channel_metrics = metrics_map[std::string{maybe_message->topic}];
    ++channel_metrics.msgs;
    channel_metrics.bytes += data_size;
    channel_metrics.compressed_bytes += std::min(compressed_size, data_size);
  }
  double log_duration_s = 0.0;
  if (maybe_start_time && maybe_end_time)
  {
    log_duration_s = std::chrono::duration<double>(*maybe_end_time - *maybe_start_time).count();
  }
  size_t total_msgs{};
  size_t total_bytes{};
  size_t total_compressed_bytes{};
  for (const auto& [channel_name, channel_metrics] : metrics_map)
  {
    total_msgs += channel_metrics.msgs;
    total_bytes += channel_metrics.bytes;
    total_compressed_bytes += channel_metrics.compressed_bytes;
    fmt::print("{}: {} msgs", channel_name, channel_metrics.msgs);
    if (log_duration_s != 0.0)
    {
      fmt::print(" ({})", human_readable_value(static_cast<double>(channel_metrics.msgs) / log_duration_s, "hz"));
    }
    fmt::println(
      " ratio: {:.3f}",
      static_cast<double>(channel_metrics.bytes) / static_cast<double>(channel_metrics.compressed_bytes));
    fmt::print("    uncompressed: {}", human_readable_value(channel_metrics.bytes, "B"));
    if (log_duration_s != 0.0)
    {
      fmt::print(" ({})", human_readable_value(static_cast<double>(channel_metrics.bytes) / log_duration_s, "B/sec"));
    }
    fmt::print(" compressed: {}", human_readable_value(channel_metrics.compressed_bytes, "B"));
    if (log_duration_s != 0.0)
    {
      fmt::print(
        " ({})", human_readable_value(static_cast<double>(channel_metrics.compressed_bytes) / log_duration_s, "B/sec"));
    }
    fmt::println("");
  }
  fmt::print("Total: {} msgs", total_msgs);
  if (log_duration_s != 0.0)
  {
    fmt::print(" ({})", human_readable_value(static_cast<double>(total_msgs) / log_duration_s, "hz"));
  }
  fmt::println(" ratio: {:.3f}", static_cast<double>(total_bytes) / static_cast<double>(total_compressed_bytes));
  fmt::print("    uncompressed: {}", human_readable_value(total_bytes, "B"));
  if (log_duration_s != 0.0)
  {
    fmt::print(" ({})", human_readable_value(static_cast<double>(total_bytes) / log_duration_s, "B/sec"));
  }
  fmt::print(" compressed: {}", human_readable_value(total_compressed_bytes, "B"));
  if (log_duration_s != 0.0)
  {
    fmt::print(" ({})", human_readable_value(static_cast<double>(total_compressed_bytes) / log_duration_s, "B/sec"));
  }
  fmt::println("");
  return {};
}

} // namespace clockwork_logging::onboard

int main(int32_t argc, char* argv[])
{
  try
  {
    TCLAP::CmdLine cmd("Onboard log metrics", ' ', "1.0", true);
    const TCLAP::MultiArg<std::string> channel_arg("c", "channel", "Channel name ", false, "name", cmd);
    const TCLAP::UnlabeledValueArg<std::string> log_uri_arg("uri", "Log URI", true, "", "uri", cmd);

    cmd.parse(argc, argv);

    const auto& log_uri = log_uri_arg.getValue();
    const auto& channels = channel_arg.getValue();

    const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};

    std::optional<std::pmr::unordered_set<std::pmr::string>> maybe_desired_channels;
    if (!channels.empty())
    {
      maybe_desired_channels.emplace(memory_resource);
      for (const auto& channel : channels)
      {
        maybe_desired_channels->insert(std::pmr::string{channel, memory_resource});
      }
    }

    if (const auto metrics_result =
          clockwork_logging::onboard::onboard_metrics(memory_resource, log_uri, maybe_desired_channels);
        !metrics_result)
    {
      jewels::log_cerr_error("Failed to get log metrics: {}", metrics_result.error());
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
