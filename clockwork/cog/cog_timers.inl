// IWYU pragma: private, include "clockwork/cog/cog_timers.hh"
#pragma once

#include "clockwork/cog/cog_timers.hh"

#include "clockwork/cog/cog_passthrough_observer.hh"
#include "clockwork/cog/detail.hh"
#include "clockwork/cog/time_since_last_exec_handler.hh"
#include "clockwork/common/abstract_timer.hh"
#include "clockwork/common/process_description.hh"
#include "clockwork/pinion/observer.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_shared_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <cstddef>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

namespace clockwork
{

template <typename... Policies>
CogTimers<Policies...>::CogTimers(jewels::memory::MemoryResource resource) noexcept
  : resource_(std::move(resource))
{
}

template <typename... Policies>
bool CogTimers<Policies...>::validate() const
{
  return detail::validate_helper<TimerPtr>(timers_, [](const auto& timer) { return timer != nullptr; });
}

template <typename... Policies>
template <typename CogType>
jewels::expected<std::shared_ptr<pinion::Observer>, jewels::MonoError> CogTimers<Policies...>::set_handle(
  jewels::Uuid<common::EndpointClassId> endpoint_id,
  std::shared_ptr<AbstractTimer> abstract_timer,
  jewels::memory::ObjectPtr<CogType> cog)
{
  std::shared_ptr<pinion::Observer> observer = {};
  auto try_set = [this, &observer, &endpoint_id, &abstract_timer, &cog](auto& timer) -> bool
  {
    using TimerType = typename std::decay_t<decltype(timer)>::element_type;
    if (endpoint_id == TimerType::endpoint_id)
    {
      timer = jewels::memory::make_pmr_shared<TimerType>(
        resource_,
        std::move(abstract_timer),
        jewels::memory::make_pmr_shared<CogPassthroughObserver<CogType>>(resource_, cog));
      observer = timer;
      return true;
    }
    return false;
  };

  auto is_set = std::apply([&try_set](auto&... timer) -> bool { return (try_set(timer) || ...); }, timers_);

  if (is_set)
  {
    return observer;
  }

  return jewels::unexpected(jewels::MonoError{});
}

template <typename... Policies>
template <std::size_t index>
auto CogTimers<Policies...>::make_condition(jewels::time::SyncTime now)
{
  return std::get<index>(timers_)->make_condition(now);
}

template <typename... Policies>
auto CogTimers<Policies...>::make_conditions(jewels::time::SyncTime now) -> ConditionsTuple
{
  return std::apply([now](auto&... timer) { return std::make_tuple(timer->make_condition(now)...); }, timers_);
}

template <typename... Policies>
jewels::expected<void, jewels::MonoError>
CogTimers<Policies...>::update_last_exec_time(jewels::time::SyncTime last_exec_time, const ConditionsTuple& conditions)
{
  // This calls timer->update_last_exec_time for every pair of {timer,
  // condition} in {timers_, conditions}. The results are combined (via the fold
  // expression) into a single boolean which is true IFF all calls succeeded.
  auto result = [last_exec_time, timers = timers_, conditions]<std::size_t... i>(std::index_sequence<i...>)
  {
    return (
      [last_exec_time](auto& timer, const auto& condition)
      {
        return timer->update_last_exec_time(last_exec_time, condition.is_active());
      }(std::get<i>(timers), std::get<i>(conditions)) &&
      ...);
  }(std::make_index_sequence<std::tuple_size_v<ConditionsTuple>>());

  if (result)
  {
    return {};
  }
  return jewels::unexpected{jewels::MonoError{}};
}

template <typename... Policies>
jewels::expected<void, jewels::MonoError> CogTimers<Policies...>::prime(jewels::time::SyncTime start_time)
{
  // To initialize the timers we pretend the last execution was at the start so
  // time_since_last_exec will be active at start + duration.
  return update_last_exec_time(start_time, ConditionsTuple{});
}

} // namespace clockwork
