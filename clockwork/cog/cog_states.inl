// IWYU pragma: private, include "clockwork/cog/cog_states.hh"
#pragma once

#include "clockwork/cog/cog_states.hh"

#include "clockwork/cog/cog_passthrough_observer.hh"
#include "clockwork/cog/cog_state.hh"
#include "clockwork/cog/detail.hh"
#include "clockwork/cog/interface.hh"
#include "clockwork/common/process_description.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_shared_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <memory>
#include <tuple>
#include <utility>

namespace clockwork
{

template <typename... Policies>
CogStates<Policies...>::CogStates(jewels::memory::MemoryResource resource)
  : resource_(std::move(resource))
{
}

template <typename... Policies>
bool CogStates<Policies...>::validate() const
{
  return detail::validate_helper<StatePtr>(cog_states_, [](const auto& state) { return state && state->validate(); });
}

template <typename... Policies>
template <typename CogType>
jewels::expected<void, jewels::MonoError> CogStates<Policies...>::set_handle(
  jewels::Uuid<common::EndpointClassId> endpoint_id,
  std::shared_ptr<CogStateData> record,
  bool /*is_shared*/,
  jewels::memory::ObjectPtr<CogType> cog)
{
  auto try_set =
    [this, &endpoint_id, &record, &cog]<typename Policy>(std::shared_ptr<CogState<Policy>>& cog_state) -> bool
  {
    using PolicyStateType = typename Policy::StateType;

    if (endpoint_id != Policy::endpoint_id)
    {
      return false;
    }

    if (auto ptr = std::dynamic_pointer_cast<CogStateDataImpl<PolicyStateType>>(std::move(record)); ptr)
    {
      ptr->observers.push_back(jewels::memory::make_pmr_shared<CogPassthroughObserver<CogType>>(resource_, cog));
      cog_state = std::make_shared<CogState<Policy>>(std::move(ptr));

      return true;
    }

    return false;
  };

  auto is_set = std::apply([&try_set](auto&... cog_state) -> bool { return (try_set(cog_state) || ...); }, cog_states_);

  if (!is_set)
  {
    return jewels::unexpected(jewels::MonoError{});
  }

  return {};
}

template <typename... Policies>
bool CogStates<Policies...>::try_lock()
{
  auto success = std::apply([](auto&... cog_state) { return (cog_state->try_lock() && ...); }, cog_states_);
  if (!success)
  {
    unlock();
  }
  return success;
}

template <typename... Policies>
bool CogStates<Policies...>::is_locked() const
{
  return std::apply([](const auto&... cog_state) { return (cog_state->is_locked() && ...); }, cog_states_);
}

namespace
{
template <typename State>
void unlock_in_reverse(State& state)
{
  state->unlock();
}

template <typename State, typename... States>
void unlock_in_reverse(State& state, States&... states)
{
  unlock_in_reverse(states...);
  state->unlock();
}
} // namespace

template <typename... Policies>
void CogStates<Policies...>::unlock()
{
  // unlock in reverse order
  if constexpr (policy_count > 0)
  {
    std::apply([](auto&... cog_state) { unlock_in_reverse(cog_state...); }, cog_states_);
  }
}

template <typename... Policies>
auto CogStates<Policies...>::make_states() -> StatesTuple
{
  return std::apply([](const auto&... cog_state) { return StatesTuple(cog_state->get_state()...); }, cog_states_);
}

} // namespace clockwork
