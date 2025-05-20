// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/exec_tools.hh"
#include "clockwork/logging/log_writer_config.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"

#include <memory>

namespace clockwork
{

// Stores the log writer and publisher configurations for when using the deterministic runner.
// In situation where the deterministic runner is not being used it is valid for these pointers to be null.
struct DeterministicLoggingConfig
{
  std::shared_ptr<const clockwork_logging::LogWriterConfigTap> log_writer_config;
  std::shared_ptr<const clockwork_logging::LogWriterConfigTap> log_publisher_config;
  bool suppress_schema_mismatch_errors = false;
};

// Stores the time range to run
struct DeterministicRunnerTimeRange
{
  jewels::time::SyncTime start;
  jewels::time::SyncTime end;
};

// Attempt to use the paths specified in execution params to retrieve the deterministic logging configurations.
// If the paths are not specified this will return a DeterministicLoggingConfig that contains nullptrs. If it is
// specified, it will populate the pointers for the config paths specifed. Will return an error on failure.
jewels::expected<DeterministicLoggingConfig, jewels::MonoError>
get_deterministic_logging_config(const ExecutionParams& execution_params);

// Uses the log to ensure that the start and end times are populated and in bounds
[[nodiscard]] jewels::expected<DeterministicRunnerTimeRange, jewels::MonoError>
calc_start_and_end_times(const ExecutionParams& execution_params);

} // namespace clockwork
