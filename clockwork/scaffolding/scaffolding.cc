// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/scaffolding/scaffolding.hh"

#include "clockwork/common/abstract_cog.hh"
#include "clockwork/pinion/shm_channel_factory.hh"
#include "clockwork/runners/epoll_manager.hh"
#include "clockwork/runners/online_cog_queue.hh"
#include "clockwork/runners/online_runner.hh"
#include "clockwork/runners/thread_pool.hh"
#include "clockwork/scaffolding/abstract_casing.hh"
#include "clockwork/scaffolding/channels.hh"
#include "clockwork/scaffolding/cog.hh"
#include "clockwork/scaffolding/config.hh"
#include "clockwork/scaffolding/io_connection.hh"
#include "clockwork/scaffolding/memory.hh"
#include "clockwork/scaffolding/state.hh"
#include "clockwork/scaffolding/timer.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"

#include <gsl/util>

#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace clockwork::scaffolding
{

// TODO(OI-2892): Refactor to reduce complexity
// NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
int run(
  const common::ProcessDescriptionTap& desc,
  AbstractCasing& casing,
  pinion::ShmChannelFactory& channel_factory,
  jewels::cli::ExitCondition& exit,
  const ExecutionParams& execution_params)
{
  auto memres = jewels::memory::MemoryResource(std::pmr::get_default_resource());
  auto memres_meta = jewels::memory::MemoryResource(std::pmr::get_default_resource());
  auto memres_config = jewels::memory::MemoryResource(std::pmr::get_default_resource());
  auto memres_scratch = jewels::memory::MemoryResource(std::pmr::get_default_resource());
  auto memres_runner = jewels::memory::MemoryResource(std::pmr::get_default_resource());

  const pinion::ShmChannelFactoryContext channel_factory_context(channel_factory);

  auto channels = setup_channels(
    desc.get_pubsub_graph().get_publish_endpoints(), memres_scratch, desc.get_process_id(), channel_factory);
  if (!channels)
  {
    return EXIT_FAILURE;
  }

  auto memory_resources =
    setup_memory_resources(desc.get_memory_resource_graph().get_memory_resources(), memres, memres_meta);
  if (!memory_resources)
  {
    return EXIT_FAILURE;
  }

  auto states = setup_states(
    desc.get_state_graph().get_state_instances(), memres_scratch, *memory_resources, channel_factory, casing);
  if (!states)
  {
    return EXIT_FAILURE;
  }

  if (!setup_configs(desc.get_config_graph().get_config_instances(), memres, memres_config, casing))
  {
    return EXIT_FAILURE;
  }

  auto timers = setup_timers(desc.get_timers(), memres);
  if (!timers)
  {
    return EXIT_FAILURE;
  }
  auto cog_queue = std::make_shared<OnlineCogQueue>(memres_runner);

  const gsl::final_action shutdown_casing{[&casing] { casing.shutdown(); }};
  auto cogs = setup_cogs(desc.get_cog_instances(), memres, memres_runner, cog_queue, casing);
  if (!cogs)
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

  if (!casing.finalize())
  {
    jewels::log_cerr_error("Casing validation failed");
    return EXIT_FAILURE;
  }

  const std::pmr::unordered_map<jewels::memory::ObjectPtr<AbstractCog>, int16_t> cog_ptr_to_gpu_ids;
  auto init_time = execution_params.start_time.value_or(jewels::time::SyncClock::now());
  if (!init_cogs(desc.get_init_cogs(), *cogs, init_time, execution_params.execution_mode, cog_ptr_to_gpu_ids))
  {
    return EXIT_FAILURE;
  }

  EPollManager epoll{memres};

  bind_channels_to_epoll(*channels, epoll);
  bind_timers_to_epoll(*timers, epoll);
  bind_io_connections_to_epoll(*io_connection_epollables, epoll);

  std::pmr::vector<CogConfig> runner_cogs{memres};
  runner_cogs.reserve(cogs->size());
  for (const auto& cog : *cogs)
  {
    runner_cogs.push_back(CogConfig{.cog = jewels::memory::make_non_null_from_ref(*cog.second)});
  }

  ThreadPool pool{ThreadPoolConfig{
    .resource = memres_runner,
    .thread_configs =
      {
        {.work = jewels::memory::make_non_null_from_ref(*cog_queue)},
        {.work = jewels::memory::make_non_null_from_ref(*cog_queue)},
        {.work = jewels::memory::make_non_null_from_ref(epoll)},
      },
  }};

  OnlineRunner runner{OnlineRunnerConfig{
    .cogs = std::move(runner_cogs),
    .pool = jewels::memory::make_non_null_from_ref(pool),
  }};

  const auto start_time = jewels::time::SyncClock::now();
  for (const auto& cog : *cogs)
  {
    if (!cog.second->prime(start_time))
    {
      jewels::log_cerr_error("Falied to prime cog '{}'.", cog.second->get_name());
      return EXIT_FAILURE;
    }
  }

  runner.start();

  exit.wait();
  return EXIT_SUCCESS;
}

} // namespace clockwork::scaffolding
