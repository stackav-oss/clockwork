// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/scaffolding/timer.hh"

#include "clockwork/common/abstract_cog.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/runners/deterministic_timer.hh"
#include "clockwork/runners/timerfd_timer.hh"
#include "clockwork/scaffolding/abstract_casing.hh"
#include "clockwork/scaffolding/cog.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/container/compare.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"

#include <xxh3.h>

#include <functional>
#include <memory_resource>
#include <stdexcept>
#include <string_view>
#include <sys/epoll.h>
#include <utility>

namespace clockwork::scaffolding
{

[[nodiscard]] jewels::expected<TimerMap, jewels::MonoError> setup_timers(
  std::span<const Tappy<common::TimerInstanceDescription<>>> descs, jewels::memory::MemoryResource memres_sys)
{
  TimerMap timers(memres_sys);
  for (const auto& desc : descs)
  {
    timers[desc.get_timer_id()] = std::make_shared<TimerfdTimer>();
  }
  return timers;
}

[[nodiscard]] jewels::expected<TimerMap, jewels::MonoError> setup_deterministic_timers(
  const ExecutionParams& /*execution_params*/,
  std::span<const Tappy<common::TimerInstanceDescription<>>> descs,
  jewels::memory::MemoryResource memres_sys)
{
  TimerMap timers(memres_sys);
  for (const auto& desc : descs)
  {
    timers[desc.get_timer_id()] = std::make_shared<DeterministicTimer>();
  }
  return timers;
}

namespace
{
template <typename TimerType>
jewels::expected<PublisherThrottleTimerVector, jewels::MonoError>
setup_publisher_throttle_timers_impl(const CogMap& cogs, jewels::memory::MemoryResource memres_sys)
{
  PublisherThrottleTimerVector timers(memres_sys);
  for (const auto& [_, cog] : cogs)
  {
    if (!cog->has_rate_limited_publishers())
    {
      continue;
    }
    auto timer = std::make_shared<TimerType>();
    if (jewels::fails(cog->set_publisher_throttle_timer(timer)))
    {
      jewels::log_cerr_error("Failed to install publisher-throttle timer for cog '{}'.", cog->get_name());
      return jewels::unexpected(jewels::MonoError{});
    }
    timers.emplace_back(std::move(timer));
  }
  return timers;
}
} // namespace

jewels::expected<PublisherThrottleTimerVector, jewels::MonoError>
setup_publisher_throttle_timers(const CogMap& cogs, jewels::memory::MemoryResource memres_sys)
{
  return setup_publisher_throttle_timers_impl<TimerfdTimer>(cogs, memres_sys);
}

jewels::expected<PublisherThrottleTimerVector, jewels::MonoError>
setup_deterministic_publisher_throttle_timers(const CogMap& cogs, jewels::memory::MemoryResource memres_sys)
{
  return setup_publisher_throttle_timers_impl<DeterministicTimer>(cogs, memres_sys);
}

[[nodiscard]] jewels::expected<std::pmr::vector<std::shared_ptr<pinion::Observer>>, jewels::MonoError> connect_timers(
  std::span<const Tappy<common::TimerInstanceDescription<>>> descs,
  jewels::memory::MemoryResource memres,
  const TimerMap& timers,
  AbstractCasing& casing)
{
  std::pmr::vector<std::shared_ptr<pinion::Observer>> observers(memres);
  observers.reserve(timers.size());
  for (const auto& desc : descs)
  {
    auto timer_it = timers.find(desc.get_timer_id());
    if (timer_it == timers.end())
    {
      jewels::log_cerr_error(
        "error connecting timer '{}': missing timer '{}'", desc.get_instance_path_name(), desc.get_timer_id());
      return jewels::unexpected(jewels::MonoError());
    }
    auto observer = casing.try_connect_timer(desc.get_timer_id(), timer_it->second);
    if (!observer)
    {
      jewels::log_cerr_error("try_connect_timer {} failed: {}", desc.get_instance_path_name(), observer.error());
      return jewels::unexpected(jewels::MonoError());
    }
    if (observer.value() == nullptr)
    {
      jewels::log_cerr_error("Got nullptr observer for '{}'", desc.get_instance_path_name());
      return jewels::unexpected(jewels::MonoError());
    }
    timer_it->second->set_observer(observer->get());
    observers.emplace_back(std::move(*observer));
  }
  return observers;
}

void bind_timers_to_epoll(const TimerMap& timers, AbstractEPollManager& epoll)
{
  for (const auto& [_, timer] : timers)
  {
    if (!epoll.add(timer->descriptor(), EPOLLIN, timer))
    {
      throw std::runtime_error("internal error: could not add event to epoll");
    }
  }
}

void bind_timers_to_epoll(const PublisherThrottleTimerVector& timers, AbstractEPollManager& epoll)
{
  for (const auto& timer : timers)
  {
    if (!epoll.add(timer->descriptor(), EPOLLIN, timer))
    {
      throw std::runtime_error("internal error: could not add publisher-throttle timer event to epoll");
    }
  }
}

} // namespace clockwork::scaffolding
