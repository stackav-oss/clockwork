// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/scaffolding/deterministic_logging_config.hh"

#include "clockwork/common/exec_tools.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/log_writer_config_clk_cc.hh"
#include "clockwork/logging/readers/log_reader.hh"
#include "clockwork/logging/readers/types.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/container/compare.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"

#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace clockwork
{

namespace
{

template <typename T>
jewels::BinaryOutcome load_tachyon_config(jewels::Out<std::shared_ptr<const Tappy<T>>> out, const std::string& path)
{
  auto status = read_tachyon_config_to_heap<Tappy<T>>(path);
  if (!status)
  {
    return jewels::failure;
  }
  *out = *status;
  return jewels::success;
}

template <typename T>
jewels::BinaryOutcome
load_tachyon_if_path(jewels::Out<std::shared_ptr<const Tappy<T>>> out, const std::optional<std::string>& path)
{
  if (!path)
  {
    return jewels::success;
  }
  std::shared_ptr<const Tappy<T>> temp;
  if (jewels::fails(load_tachyon_config<T>(jewels::Out{temp}, *path)))
  {
    return jewels::failure;
  }
  *out = std::move(temp);
  return jewels::success;
}

} // namespace

jewels::BinaryOutcome get_deterministic_logging_config(
  jewels::Out<DeterministicLoggingConfig> logging_config_out, const ExecutionParams& execution_params)
{
  if (execution_params.execution_mode == ExecutionMode::online)
  {
    *logging_config_out = DeterministicLoggingConfig{};
    return jewels::success;
  }
  DeterministicLoggingConfig logging_config;
  if (jewels::fails(
        load_tachyon_if_path<clockwork_logging::LogWriterConfig<>>(
          jewels::Out{logging_config.log_writer_config}, execution_params.log_writer_config_path)))
  {
    return jewels::failure;
  }
  if (jewels::fails(
        load_tachyon_if_path<clockwork_logging::ChannelPublisherConfig<>>(
          jewels::Out{logging_config.channel_publisher_config}, execution_params.channel_publisher_config_path)))
  {
    return jewels::failure;
  }
  if (jewels::fails(
        load_tachyon_if_path<clockwork::tools::MetricsChannelMetadataConfig<>>(
          jewels::Out{logging_config.metrics_channel_metadata_config},
          execution_params.metrics_channel_metadata_config_path)))
  {
    return jewels::failure;
  }
  if (jewels::fails(
        load_tachyon_if_path<clockwork::common::SignalMetadataConfig<>>(
          jewels::Out{logging_config.signal_metadata_config}, execution_params.signal_metadata_config_path)))
  {
    return jewels::failure;
  }

  if (execution_params.suppress_schema_mismatch_errors)
  {
    logging_config.suppress_schema_mismatch_errors = *execution_params.suppress_schema_mismatch_errors;
  }
  *logging_config_out = std::move(logging_config);
  return jewels::success;
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
