// IWYU pragma: private, include "clockwork/scaffolding/deterministic_runner_setup.hh"
#pragma once

#include "clockwork/scaffolding/deterministic_runner_setup.hh"

#include "clockwork/common/abstract_cog.hh"
#include "clockwork/common/abstract_cog_queue.hh"
#include "clockwork/common/abstract_timer.hh"
#include "clockwork/common/exec_tools.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/runners/channel_publisher.hh"
#include "clockwork/runners/deterministic_runner.hh"
#include "clockwork/scaffolding/channels.hh"
#include "clockwork/scaffolding/deterministic_logging_config.hh"
#include "clockwork/scaffolding/timer.hh"
#include "jewels/container/compare.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"

#include <cstdint>
#include <functional>
#include <memory>
#include <memory_resource>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace clockwork
{

template <typename LogMessageFetcherType>
// NOLINTNEXTLINE(readability-function-size) TODO(OI-2892): Refactor to reduce complexity
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
  const std::pmr::unordered_map<jewels::memory::ObjectPtr<AbstractCog>, int16_t>& cog_to_gpu_id)
{
  std::pmr::vector<std::shared_ptr<AbstractTimer>> timer_vec(memres_runner);
  for (const auto& [_, timer] : timers)
  {
    timer_vec.emplace_back(timer);
  }
  timer_vec.insert(timer_vec.end(), publisher_throttle_timers.begin(), publisher_throttle_timers.end());

  auto runner_channels = convert_channel_map(memres_runner, scaffolding_channel_map);
  if (!runner_channels)
  {
    return jewels::unexpected(jewels::MonoError{});
  }
  std::shared_ptr<AbstractChannelPublisher> channel_publisher;
  if (logging_config.channel_publisher_config && execution_params.message_injectors.message_fetcher_)
  {
    channel_publisher = std::make_shared<clockwork::ChannelPublisher>(
      memres_runner,
      jewels::memory::make_non_null_from_ref(*logging_config.channel_publisher_config),
      *execution_params.message_injectors.message_fetcher_,
      *runner_channels,
      logging_config.suppress_schema_mismatch_errors);
  }
  else if (logging_config.channel_publisher_config && execution_params.input_log_uri)
  {
    channel_publisher = std::make_shared<clockwork::ChannelPublisher>(
      memres_runner,
      jewels::memory::make_non_null_from_ref(*logging_config.channel_publisher_config),
      jewels::memory::make_shared<LogMessageFetcherType>(
        *execution_params.input_log_uri,
        jewels::memory::make_non_null_from_ref(*logging_config.channel_publisher_config),
        clockwork_logging::LogInterval{
          clockwork_logging::LogTimestamp{time_range.start}, clockwork_logging::LogTimestamp{time_range.end}},
        memres_runner),
      *runner_channels,
      logging_config.suppress_schema_mismatch_errors);
  }

  return DeterministicRunnerConfig{
    .resource = memres_runner,
    .cogs = std::move(cogs),
    .timers = timer_vec,
    .queue = std::move(queue),
    .channel_map = *runner_channels,
    .start_time = time_range.start,
    .end_time = time_range.end,
    .channel_publisher = channel_publisher,
    .cog_to_gpu_id = cog_to_gpu_id,
    .playback_speed = execution_params.playback_speed.value_or(0.0),
  };
}
} // namespace clockwork
