// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/readers/abstract_log_reader.hh"
#include "clockwork/logging/readers/log_reader_factory.hh"
#include "clockwork/logging/readers/types.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/math/constants.hh"
#include "jewels/std/expected.hh"

#include <fmt10/base.h>
#include <fmt10/format.h>
#include <tclap/CmdLine.h>
#include <tclap/ValueArg.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace clockwork_logging
{

/// Get the metrics from the log
/// @param[in] log_uri Log URI
/// @return Reader pointer or LogError on failure
[[nodiscard]] LogExpected<LogMetrics> load_metrics(std::string_view log_uri)
{
  try
  {
    auto reader_ptr = make_reader(log_uri, {}, {});
    return reader_ptr->get_metrics();
  }
  catch (const std::invalid_argument& exc)
  {
    jewels::log_cerr_error("{}", exc.what());
    return jewels::unexpected(LogError::failed_to_load_metrics);
  }
}

/// Format a human readable value
/// @param[in] value Value to print
/// @param[in] units_suffix
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
/// @param[in] units_suffix
/// @return Human readable value string
std::string human_readable_value(uint64_t value, std::string_view units_suffix)
{
  if (value < jewels::math::constants::bytes_per_kb<uint64_t>)
  {
    return fmt::format("{} {}", value, units_suffix);
  }
  return human_readable_value(static_cast<double>(value), units_suffix);
}

/// Print the log metrics
/// @param[in] log_metrics Log metrics
void print_metrics(const LogMetrics& log_metrics)
{
  fmt::print("\n");
  const auto start_time = log_metrics.transmit_time_interval.get_start_time();
  const auto start_time_t = std::chrono::system_clock::to_time_t(start_time);
  constexpr size_t date_buffer_size = 100U;
  std::array<char, date_buffer_size> date_buffer{};
  std::string_view time_str;
  struct tm tm_buf{};
  auto* tm_ptr = gmtime_r(&start_time_t, &tm_buf);
  if (tm_ptr == nullptr)
  {
    time_str = "GMT_CONVERSION_ERROR";
  }
  else if (const auto t_len = std::strftime(date_buffer.data(), date_buffer.size(), "%FT%T UTC", tm_ptr); t_len != 0)
  {
    time_str = date_buffer.data();
  }
  else
  {
    time_str = "TIME_FORMAT_ERROR";
  }
  fmt::print(
    "Start Time:    {} ({:.9f})\n", time_str, std::chrono::duration<double>(start_time.time_since_epoch()).count());
  const auto end_time = log_metrics.transmit_time_interval.get_end_time();
  const auto end_time_t = std::chrono::system_clock::to_time_t(end_time);
  tm_ptr = gmtime_r(&end_time_t, &tm_buf);
  if (tm_ptr == nullptr)
  {
    time_str = "GMT_CONVERSION_ERROR";
  }
  else if (const auto t_len = std::strftime(date_buffer.data(), date_buffer.size(), "%FT%T UTC", tm_ptr); t_len != 0)
  {
    time_str = date_buffer.data();
  }
  else
  {
    time_str = "TIME_FORMAT_ERROR";
  }
  fmt::print(
    "End Time:      {} ({:.9f})\n", time_str, std::chrono::duration<double>(end_time.time_since_epoch()).count());
  const std::chrono::duration<double> log_duration = log_metrics.transmit_time_interval.get_duration();
  const auto log_duration_s = log_duration.count();
  fmt::print("Duration:      {:.3f} seconds\n", log_duration_s);
  fmt::print("Messages:      {}", log_metrics.message_count);
  if (log_duration_s != 0.0)
  {
    fmt::print(" ({})", human_readable_value(static_cast<double>(log_metrics.message_count) / log_duration_s, "hz"));
  }
  fmt::print("\n");
  if (log_metrics.byte_count != 0)
  {
    fmt::print("Bytes:         {}", human_readable_value(log_metrics.byte_count, "B"));
    if (log_duration_s != 0.0)
    {
      fmt::print(" ({})", human_readable_value(static_cast<double>(log_metrics.byte_count) / log_duration_s, "B/sec"));
    }
    fmt::print("\n");
  }
  fmt::print("\nChannels:\n");
  size_t max_topic_size = 0U;
  for (const auto& metrics : log_metrics.topic_metrics)
  {
    max_topic_size = std::max(max_topic_size, metrics.topic.size());
  }
  for (const auto& metrics : log_metrics.topic_metrics)
  {
    const std::string topic_pad(max_topic_size - metrics.topic.size(), ' ');
    fmt::print("  {}{} : {} msgs", metrics.topic, topic_pad, metrics.message_count);
    if (log_duration_s != 0.0)
    {
      fmt::print(" ({})", human_readable_value(static_cast<double>(metrics.message_count) / log_duration_s, "hz"));
    }
    if (metrics.byte_count != 0)
    {
      fmt::print("    {}", human_readable_value(metrics.byte_count, "B"));
      if (log_duration_s != 0.0)
      {
        fmt::print(" ({})", human_readable_value(static_cast<double>(metrics.byte_count) / log_duration_s, "B/sec"));
      }
    }
    fmt::print("\n");
  }
}

} // namespace clockwork_logging

int main(int32_t argc, char* argv[])
{
  try
  {
    TCLAP::CmdLine cmd("log metrics", ' ', "1.0", true);
    const TCLAP::ValueArg<std::string> log_uri_arg("l", "log-uri", "Log URI", true, "", "uri", cmd);

    cmd.parse(argc, argv);

    const auto& log_uri = log_uri_arg.getValue();

    const auto metrics_result = clockwork_logging::load_metrics(log_uri);
    if (!metrics_result)
    {
      jewels::log_cerr_error("Failed to get log_metrics: {}", metrics_result.error());
      return 1;
    }
    clockwork_logging::print_metrics(metrics_result.value());
    return 0;
  }
  catch (const std::exception& exc)
  {
    std::cerr << "Caught unexpected exception: " << exc.what() << '\n';
    return 1;
  }
}
