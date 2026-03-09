// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/scaffolding/deterministic_logging_config.hh"

#include "clockwork/common/exec_tools.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/log_writer_config_clk_cc.hh"
#include "clockwork/logging/readers/log_reader.hh"
#include "clockwork/logging/readers/types.hh"
#include "jewels/container/compare.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"

#include <memory>
#include <optional>
#include <string>

namespace clockwork
{
jewels::expected<DeterministicLoggingConfig, jewels::MonoError>
get_deterministic_logging_config(const ExecutionParams& execution_params)
{
  if (execution_params.execution_mode == ExecutionMode::online)
  {
    return DeterministicLoggingConfig{};
  }
  DeterministicLoggingConfig logging_config;
  if (execution_params.log_writer_config_path)
  {
    auto log_writer_config_status = read_tachyon_config_to_heap<Tappy<clockwork_logging::LogWriterConfig<>>>(
      *execution_params.log_writer_config_path);
    if (!log_writer_config_status)
    {
      return jewels::unexpected(jewels::MonoError{});
    }
    logging_config.log_writer_config = *log_writer_config_status;
  }
  if (execution_params.channel_publisher_config_path)
  {
    auto channel_publisher_config_status =
      read_tachyon_config_to_heap<Tappy<clockwork_logging::ChannelPublisherConfig<>>>(
        *execution_params.channel_publisher_config_path);
    if (!channel_publisher_config_status)
    {
      return jewels::unexpected(jewels::MonoError{});
    }
    logging_config.channel_publisher_config = *channel_publisher_config_status;
  }
  if (execution_params.metrics_channel_metadata_config_path)
  {
    auto metrics_channel_metadata_config_status =
      read_tachyon_config_to_heap<Tappy<clockwork::tools::MetricsChannelMetadataConfig<>>>(
        *execution_params.metrics_channel_metadata_config_path);
    if (!metrics_channel_metadata_config_status)
    {
      return jewels::unexpected(jewels::MonoError{});
    }
    logging_config.metrics_channel_metadata_config = *metrics_channel_metadata_config_status;
  }

  if (execution_params.suppress_schema_mismatch_errors)
  {
    logging_config.suppress_schema_mismatch_errors = *execution_params.suppress_schema_mismatch_errors;
  }
  return logging_config;
}

[[nodiscard]] jewels::expected<DeterministicRunnerTimeRange, jewels::MonoError>
calc_start_and_end_times(const ExecutionParams& execution_params)
{
  const std::optional<std::string>& input_log_uri = execution_params.input_log_uri;
  std::optional<jewels::time::SyncTime> start_time = execution_params.start_time;
  std::optional<jewels::time::SyncTime> end_time = execution_params.end_time;

  // The start and end time for the simulation is defaulted to the start and end time of the input log file. However,
  // that is overridden if a start or end time is passed in.
  // Either the start or end time is not set so we need to obtain it from the log file.

  if (start_time && end_time)
  {
    return {{.start = *start_time, .end = *end_time}};
  }

  if (!input_log_uri)
  {
    jewels::log_cerr_error(
      "If running with the deterministic runner, either an input log uri must be specified or "
      "both the start and end time of the simulation must be specified.");
    return jewels::unexpected(jewels::MonoError{});
  }

  auto log_reader = clockwork_logging::LogReader(input_log_uri.value(), {}, {});
  if (!start_time)
  {
    auto start_time_status = log_reader.start_time();
    if (!start_time_status)
    {
      jewels::log_cerr_error(
        "Error obtaining start time from input log file. Error: {} URI: {}. Please verify that the uri is valid",
        start_time_status.error(),
        input_log_uri.value());
      return jewels::unexpected(jewels::MonoError{});
    }
    start_time.emplace(start_time_status.value().get_time());
  }
  if (!end_time)
  {
    auto end_time_status = log_reader.end_time();
    if (!end_time_status)
    {
      jewels::log_cerr_error(
        "Error obtaining end time from input log file. Error: {} URI: {}. Please verify that the uri is valid",
        end_time_status.error(),
        input_log_uri.value());
      return jewels::unexpected(jewels::MonoError{});
    }
    end_time.emplace(end_time_status.value().get_time());
  }

  return {{.start = *start_time, .end = *end_time}};
}

} // namespace clockwork
