// IWYU pragma: private, include "clockwork/scaffolding/scaffolding_offline_impl.hh"
#pragma once
#include "clockwork/scaffolding/scaffolding_offline_impl.hh"

#include "clockwork/common/abstract_cog.hh"
#include "clockwork/common/exec_tools.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/logging/channel_publisher_config_clk_cc.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/pinion/abstract_channel_factory.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/runners/channel_publisher.hh"
#include "clockwork/runners/deterministic_cog_queue.hh"
#include "clockwork/runners/deterministic_runner.hh"
#include "clockwork/scaffolding/abstract_casing.hh"
#include "clockwork/scaffolding/channels.hh"
#include "clockwork/scaffolding/cog.hh"
#include "clockwork/scaffolding/config.hh"
#include "clockwork/scaffolding/data_source_loader.hh"
#include "clockwork/scaffolding/deterministic_logging_config.hh"
#include "clockwork/scaffolding/deterministic_runner_setup.hh"
#include "clockwork/scaffolding/first_message_cache.hh"
#include "clockwork/scaffolding/io_connection.hh"
#include "clockwork/scaffolding/memory.hh"
#include "clockwork/scaffolding/snapshots.hh"
#include "clockwork/scaffolding/state.hh"
#include "clockwork/scaffolding/timer.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/cli/exit_condition.hh"
#include "jewels/container/compare.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/scope_guard/scope_guard.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"
#include "jewels/uuid/uuid_hasher.hh"

#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace clockwork::scaffolding
{

template <typename LogMessageFetcherType>
// TODO(OI-2892): Refactor to reduce complexity
// NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
int run_deterministic_impl(
  const Tappy<common::ProcessDescription<>>& desc,
  AbstractCasing& casing,
  pinion::AbstractChannelFactory& channel_factory,
  jewels::cli::ExitCondition& exit,
  const ExecutionParams& execution_params)
{
  auto memres = jewels::memory::MemoryResource(std::pmr::get_default_resource());
  auto memres_meta = jewels::memory::MemoryResource(std::pmr::get_default_resource());
  auto memres_config = jewels::memory::MemoryResource(std::pmr::get_default_resource());
  auto memres_scratch = jewels::memory::MemoryResource(std::pmr::get_default_resource());
  auto memres_runner = jewels::memory::MemoryResource(std::pmr::get_default_resource());

  const pinion::ChannelFactoryContext channel_factory_context(channel_factory);

  auto time_range = calc_start_and_end_times(execution_params);
  if (!time_range)
  {
    return EXIT_FAILURE;
  }

  DeterministicLoggingConfig deterministic_logging_config;
  if (jewels::fails(get_deterministic_logging_config(jewels::Out{deterministic_logging_config}, execution_params)))
  {
    return EXIT_FAILURE;
  }

  auto cog_gpu_assignment_config = get_cog_gpu_assignment_config(execution_params);
  if (!cog_gpu_assignment_config)
  {
    return EXIT_FAILURE;
  }

  auto logged_channels =
    (deterministic_logging_config.channel_publisher_config
       ? deterministic_logging_config.channel_publisher_config->get_channels()
       : std::span<const Tappy<clockwork_logging::PublishedChannelConfig<>>>{});
  auto channels = setup_deterministic_channels(
    desc.get_pubsub_graph().get_publish_endpoints(), logged_channels, memres_scratch, channel_factory);
  if (!channels)
  {
    return EXIT_FAILURE;
  }
  auto deterministic_log_writer = setup_deterministic_log_writer(
    memres_scratch, execution_params, deterministic_logging_config, *channels, time_range->start);
  if (!deterministic_log_writer)
  {
    return EXIT_FAILURE;
  }

  auto memory_resources =
    setup_memory_resources(desc.get_memory_resource_graph().get_memory_resources(), memres, memres_meta);
  if (!memory_resources)
  {
    return EXIT_FAILURE;
  }
  // Populate first message cache for data source restoration
  scaffolding::FirstMessageCache first_message_cache(memres);

  if (deterministic_logging_config.channel_publisher_config)
  {
    std::shared_ptr<MessageFetcher> message_fetcher{nullptr};
    if (execution_params.message_injectors.message_fetcher_)
    {
      message_fetcher = execution_params.message_injectors.message_fetcher_.value().get();
    }
    else
    {
      if (!execution_params.input_log_uri)
      {
        jewels::log_cerr_error("Input log URI must be specified to restore from log");
        return EXIT_FAILURE;
      }
      message_fetcher = std::make_shared<LogMessageFetcherType>(
        *execution_params.input_log_uri,
        jewels::memory::make_non_null_from_ref(*deterministic_logging_config.channel_publisher_config),
        clockwork_logging::LogInterval{
          clockwork_logging::LogTimestamp{time_range->start}, clockwork_logging::LogTimestamp{time_range->end}},
        memres_runner);
    }

    auto& data_source_message_fetcher = *message_fetcher;

    auto init_result = data_source_message_fetcher.initialize();
    if (!init_result)
    {
      jewels::log_cerr_error("Failed to initialize temporary message fetcher");
      return EXIT_FAILURE;
    }

    if (jewels::fails(populate_first_message_cache(
          jewels::Out{first_message_cache}, desc.get_data_sources(), data_source_message_fetcher, memres)))
    {
      jewels::log_cerr_error("Failed to populate first message cache from log");
      return EXIT_FAILURE;
    }

    if (jewels::fails(data_source_message_fetcher.reset()))
    {
      jewels::log_cerr_error("Failed to reset temporary message fetcher");
      return EXIT_FAILURE;
    }
  }

  auto states = setup_states(
    desc.get_state_graph().get_state_instances(),
    memres_scratch,
    *memory_resources,
    channel_factory,
    casing,
    desc.get_data_sources(),
    first_message_cache);
  if (!states)
  {
    return EXIT_FAILURE;
  }

  // NOTE: this must be called after states is created but before anything else
  // that can fail and cause an early return, since the CasingImpl destructor (called on early return)
  // accesses the states vector to clear publishers.
  // If we call the destructors on the state objects they will try to access the publishers which have already been
  // freed, causing a use-after-free and crash.
  const jewels::ScopeGuard shutdown_casing{[&casing] { casing.shutdown(); }};

  if (!setup_configs(
        desc.get_config_graph().get_config_instances(),
        desc.get_data_sources(),
        memres,
        memres_config,
        first_message_cache,
        casing))
  {
    return EXIT_FAILURE;
  }

  // Clear cache after restoration to free memory before execution
  first_message_cache.clear();

  auto timers = setup_deterministic_timers(execution_params, desc.get_timers(), memres);
  if (!timers)
  {
    return EXIT_FAILURE;
  }

  auto cog_queue = std::make_shared<DeterministicCogQueue>(memres_runner);

  auto cogs = setup_cogs(desc.get_cog_instances(), memres, memres_runner, cog_queue, casing);
  if (!cogs)
  {
    return EXIT_FAILURE;
  }
  auto publisher_throttle_timers = setup_deterministic_publisher_throttle_timers(*cogs, memres);
  if (!publisher_throttle_timers)
  {
    return EXIT_FAILURE;
  }

  auto io_connection_epollables = setup_io_connections(desc.get_io_connections(), memres, casing);
  if (!io_connection_epollables)
  {
    return EXIT_FAILURE;
  }

  if (!connect_memory_resources(desc.get_memory_resource_graph().get_connections(), *memory_resources, casing))
  {
    return EXIT_FAILURE;
  }

  if (!connect_configs(desc.get_config_graph().get_connections(), casing))
  {
    return EXIT_FAILURE;
  }

  if (!connect_states(desc.get_state_graph().get_connections(), memres_scratch, casing))
  {
    return EXIT_FAILURE;
  }

  auto channel_observers =
    connect_subscribers(desc.get_pubsub_graph().get_connections(), memres, *channels, desc.get_process_id(), casing);
  if (!channel_observers)
  {
    return EXIT_FAILURE;
  }

  if (!connect_publishers(desc.get_pubsub_graph().get_publish_endpoints(), *channels, desc.get_process_id(), casing))
  {
    return EXIT_FAILURE;
  }

  auto timer_observers = connect_timers(desc.get_timers(), memres, *timers, casing);
  if (!timer_observers)
  {
    return EXIT_FAILURE;
  }

  auto non_connected_channels =
    setup_non_connected_channels(desc.get_not_connected_endpoints(), casing, memres_scratch, channel_factory);

  // Snapshots must be configured after configs and states are connected
  if (fails(setup_snapshot_configs(desc.get_snapshot_configs(), casing)))
  {
    return EXIT_FAILURE;
  }

  if (!casing.finalize())
  {
    jewels::log_cerr_error("Casing validation failed");
    return EXIT_FAILURE;
  }

  std::pmr::vector<CogConfig> runner_cogs{memres};
  runner_cogs.reserve(cogs->size());
  std::pmr::unordered_map<jewels::memory::ObjectPtr<AbstractCog>, int16_t> cog_ptr_to_gpu_ids;
  for (const auto& cog : *cogs)
  {
    auto cog_ptr = jewels::memory::make_non_null_from_ref(*cog.second);
    runner_cogs.push_back(CogConfig{.cog = cog_ptr});
    // associate cogs with their gpu
    const auto& cog_uuid = cog.first;
    if (!cog_gpu_assignment_config->cog_uuid_to_gpu_id.empty())
    {
      if (cog_gpu_assignment_config->cog_uuid_to_gpu_id.contains(cog_uuid))
      {
        cog_ptr_to_gpu_ids[cog_ptr] = cog_gpu_assignment_config->cog_uuid_to_gpu_id.at(cog_uuid);
      }
      else
      {
        cog_ptr_to_gpu_ids[cog_ptr] = 0; // default gpu
      }
    }
  }

  if (!init_cogs(desc.get_init_cogs(), *cogs, time_range->start, execution_params.execution_mode, cog_ptr_to_gpu_ids))
  {
    return EXIT_FAILURE;
  }

  // make sure each gpu mapping is legitimate
  for (const auto& [cog_uuid, gpu_id] : cog_gpu_assignment_config->cog_uuid_to_gpu_id)
  {
    bool cog_exists = false;
    for (const auto& cog : *cogs)
    {
      if (cog.first == cog_uuid)
      {
        cog_exists = true;
        break;
      }
    }
    if (!cog_exists)
    {
      jewels::log_cerr_error("Cog being assigned to a gpu is not available: {}", cog_uuid);
      return EXIT_FAILURE;
    }
  }

  auto deterministic_runner_config = build_deterministic_runner_config<LogMessageFetcherType>(
    execution_params,
    memres_runner,
    std::move(runner_cogs),
    *timers,
    *publisher_throttle_timers,
    *channels,
    std::move(cog_queue),
    deterministic_logging_config,
    *time_range,
    cog_ptr_to_gpu_ids);

  if (!deterministic_runner_config)
  {
    jewels::log_cerr_error("Error configuring the deterministic runner");
    return EXIT_FAILURE;
  }
  auto runner = DeterministicRunner(*deterministic_runner_config);

  for (const auto& cog : *cogs)
  {
    if (!cog.second->prime(time_range->start))
    {
      jewels::log_cerr_error("Failed to prime cog '{}'.", cog.second->get_name());
      return EXIT_FAILURE;
    }
  }

  if (jewels::fails(runner.start(time_range->start, time_range->end, exit)))
  {
    jewels::log_cerr_error("Failed to start runner.");
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
} // namespace clockwork::scaffolding
