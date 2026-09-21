// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/abstract_cog.hh"
#include "clockwork/common/abstract_cog_queue.hh"
#include "clockwork/common/exec_tools.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/logging/writers/deterministic_log_writer.hh"
#include "clockwork/runners/deterministic_channel_handler.hh"
#include "clockwork/runners/deterministic_runner.hh"
#include "clockwork/scaffolding/channels.hh"
#include "clockwork/scaffolding/deterministic_logging_config.hh"
#include "clockwork/scaffolding/timer.hh"
#include "jewels/container/compare.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"
#include "jewels/uuid/uuid_hasher.hh"

#include <cstdint>
#include <functional>
#include <memory>
#include <memory_resource>
#include <unordered_map>
#include <vector>

namespace clockwork
{

// Per-cog GPU assignments
struct GpuAssignmentConfig
{
  std::pmr::unordered_map<jewels::Uuid<common::CogInstanceId>, int16_t, jewels::UuidHasher<common::CogInstanceId>>
    cog_uuid_to_gpu_id{};
};

/// Helper function to build the deterministic log writer
/// Helper function to convert a scaffolding channel map to a logging channel map.
/// @param[in] memory_resource Memory resource
/// @param[in] scaffolding_channel_map Scaffolding channel map
/// @return Logging channel map or MonoError instance of failure
jewels::expected<clockwork_logging::ChannelMap, jewels::MonoError> convert_channel_map(
  jewels::memory::MemoryResource memory_resource, const scaffolding::ChannelMap& scaffolding_channel_map);

/// Helper function to build the deterministic log writer
/// @param[in] memres Memory resource
/// @param[in] execution_params Execution parameters
/// @param[in] logging_config Deterministic log writer configuration
/// @param[in] scaffolding_channel_map Scaffolding channel map
/// @param[in] init_time The start time for execution
/// @return Deterministic channel handler or MonoError instance of failure
jewels::expected<std::shared_ptr<DeterministicChannelHandler>, jewels::MonoError> setup_deterministic_log_writer(
  jewels::memory::MemoryResource memres,
  const ExecutionParams& execution_params,
  const DeterministicLoggingConfig& logging_config,
  const scaffolding::ChannelMap& scaffolding_channel_map,
  jewels::time::SyncTime init_time);

/// Helper function to build the deterministic runner config.
/// @tparam LogMessageFetcherType Log message fetcher type
/// @param[in] execution_params Runner execution parameters
/// @param[in] memres_runner Memory resource used by the runner
/// @param[in] cogs Cogs to run
/// @param[in] timers Timers to run
/// @param[in] scaffolding_channel_map Scaffolding channel map
/// @param[in] queue Cog queue
/// @param[in] logging_config Deterministic log writer configuration
/// @param[in] time_range Deterministic runner time range
template <typename LogMessageFetcherType>
jewels::expected<DeterministicRunnerConfig, jewels::MonoError> build_deterministic_runner_config(
  const ExecutionParams& execution_params,
  jewels::memory::MemoryResource memres_runner,
  std::pmr::vector<CogConfig> cogs,
  const scaffolding::TimerMap& timers,
  const scaffolding::PublisherThrottleTimerVector& publisher_throttle_timers,
  const scaffolding::ChannelMap& scaffolding_channel_map,
  std::shared_ptr<AbstractCogQueue> queue,
  const DeterministicLoggingConfig& logging_config,
  const DeterministicRunnerTimeRange& time_range,
  const std::pmr::unordered_map<jewels::memory::ObjectPtr<AbstractCog>, int16_t>& cog_to_gpu_id);

// Attempt to use the paths specified in execution params to retrieve the cog-gpu mappings configuration.
// If the paths are not specified this will return a CogGpuAssignmentConfig with no assignments.
// Will return an error on failure.
jewels::expected<GpuAssignmentConfig, jewels::MonoError>
get_cog_gpu_assignment_config(const ExecutionParams& execution_params);

} // namespace clockwork

#include "clockwork/scaffolding/deterministic_runner_setup.inl"
