// IWYU pragma: private, include "clockwork/cog/cog_states.hh"
#pragma once

#include "clockwork/cog/cog_states.hh"

#include "clockwork/cog/cog_passthrough_observer.hh"
#include "clockwork/cog/cog_state.hh"
#include "clockwork/cog/detail.hh"
#include "clockwork/cog/interface.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/pinion/publishable.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/container/compare.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_shared_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <chrono>
#include <cstddef>
#include <memory>
#include <memory_resource>
#include <optional>
#include <string>
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
jewels::BinaryOutcome CogStates<Policies...>::set_publisher_handle(
  jewels::Uuid<common::EndpointClassId> endpoint_id, pinion::PublisherHandle&& publisher)
{
  auto try_set = [&endpoint_id, &publisher]<typename Policy>(std::shared_ptr<CogState<Policy>>& cog_state) -> bool
  {
    if (endpoint_id != Policy::endpoint_id)
    {
      return false;
    }

    if (!cog_state)
    {
      jewels::log_cerr_error("State not initialized for endpoint {}", endpoint_id.to_string());
      return false;
    }

    if (cog_state->get_snapshot_info())
    {
      jewels::log_cerr_error("Snapshot publisher already set for state endpoint {}", endpoint_id.to_string());
      return false;
    }
    cog_state->set_snapshot_info({.snapshot_publisher = std::move(publisher), .interval = {}, .cycles = {}});
    return true;
  };

  const bool matched = std::apply([&try_set](auto&... cog_state) { return (try_set(cog_state) || ...); }, cog_states_);

  return matched ? jewels::success : jewels::failure;
}

template <typename... Policies>
jewels::BinaryOutcome CogStates<Policies...>::set_snapshot_config(
  jewels::Uuid<common::EndpointClassId> endpoint_id, const Tappy<common::SnapshotConfig>& snapshot_config)
{
  auto try_set = [&endpoint_id, &snapshot_config]<typename Policy>(std::shared_ptr<CogState<Policy>>& cog_state) -> auto
  {
    if (endpoint_id != Policy::endpoint_id)
    {
      return jewels::failure;
    }

    if (!cog_state || !cog_state->validate())
    {
      jewels::log_cerr_error("State not initialized for endpoint '{}'", endpoint_id.to_string());
      return jewels::failure;
    }

    auto* existing_info = cog_state->get_snapshot_info();
    if (!existing_info)
    {
      jewels::log_cerr_error("Snapshot publisher not set for state endpoint '{}'", endpoint_id.to_string());
      return jewels::failure;
    }

    existing_info->interval = snapshot_config.has_interval()
                                ? std::optional{std::chrono::nanoseconds(snapshot_config.value_interval())}
                                : std::nullopt;
    existing_info->cycles = snapshot_config.has_cycles() ? std::optional{snapshot_config.value_cycles()} : std::nullopt;

    return jewels::success;
  };

  bool matched = std::apply([&try_set](auto&... cog_state) { return (ok(try_set(cog_state)) || ...); }, cog_states_);

  return matched ? jewels::success : jewels::failure;
}

namespace detail
{
/// Check if a snapshot should be taken and update execution count accordingly
/// @param snapshot_info The snapshot info to check
/// @param current_time The current time
/// @return True if a snapshot should be taken
template <typename SnapshotInfo>
bool should_take_snapshot(SnapshotInfo* snapshot_info, jewels::time::SyncTime current_time)
{
  if (snapshot_info->cycles)
  {
    snapshot_info->execution_count++;
    if (snapshot_info->execution_count >= *snapshot_info->cycles)
    {
      return true;
    }
  }

  if (snapshot_info->interval)
  {
    if (current_time >= snapshot_info->last_snapshot_time + *snapshot_info->interval)
    {
      return true;
    }
  }
  return false;
}

/// Publish a single snapshot for a state
/// @param snapshot_info The snapshot info containing publisher
/// @param state The state to snapshot
/// @param current_time The current time for publishing
/// @return True if successful, false otherwise
template <typename StateType, typename SnapshotInfo>
bool publish_single_snapshot(SnapshotInfo* snapshot_info, const StateType* state, jewels::time::SyncTime current_time)
{
  auto slot = snapshot_info->snapshot_publisher.reserve();
  if (!slot)
  {
    jewels::log_cerr_error("Failed to reserve slot for state snapshot");
    return false;
  }

  auto publishable = pinion::Publishable<StateType>::try_make(jewels::memory::make_non_null_from_ref(*slot));
  if (!publishable)
  {
    jewels::log_cerr_error("Failed to create publishable for state snapshot");
    return false;
  }

  publishable->message() = *state;
  publishable->mark_for_publish();
  auto result = slot->process(current_time);
  if (!result)
  {
    jewels::log_cerr_error("Failed to publish state snapshot: {}", result.error());
    return false;
  }

  return true;
}
} // namespace detail

template <typename... Policies>
jewels::BinaryOutcome CogStates<Policies...>::publish_snapshots(jewels::time::SyncTime current_time)
{
  bool all_success = true;

  auto try_snapshot = [&current_time,
                       &all_success]<typename Policy>(std::shared_ptr<CogState<Policy>>& cog_state) -> void
  {
    if (!cog_state || !cog_state->validate())
    {
      return;
    }

    auto* snapshot_info = cog_state->get_snapshot_info();
    if (!snapshot_info)
    {
      return;
    }

    if (!detail::should_take_snapshot(snapshot_info, current_time))
    {
      return;
    }

    using StateType = typename Policy::StateType;
    if constexpr (requires { typename CogStateDataImpl<StateType>::StateType; })
    {
      const bool result =
        detail::publish_single_snapshot<StateType>(snapshot_info, cog_state->get_state(), current_time);
      if (!result)
      {
        all_success = false;
      }

      snapshot_info->execution_count = 0;
      snapshot_info->last_snapshot_time = current_time;
    }
    else
    {
      jewels::log_cerr_error("StateType does not support snapshots for endpoint '{}'", Policy::endpoint_id.to_string());
      all_success = false;
    }
  };

  std::apply([&try_snapshot](auto&... cog_state) { (try_snapshot(cog_state), ...); }, cog_states_);

  return all_success ? jewels::success : jewels::failure;
}

template <typename... Policies>
bool CogStates<Policies...>::try_lock()
{
  auto result = std::apply([](auto&... cog_state) { return (cog_state->try_lock() && ...); }, cog_states_);
  if (!result)
  {
    unlock();
  }
  return result;
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

template <typename... Policies>
template <size_t index>
[[nodiscard]] bool CogStates<Policies...>::is_state_set() const
{
  return static_cast<bool>(std::get<index>(cog_states_));
}

template <typename... Policies>
template <size_t index>
jewels::BinaryOutcome CogStates<Policies...>::get_state(jewels::FactoryOut<SchemaRefType<index>> state)
{
  if (!is_state_set<index>())
  {
    return jewels::failure;
  }
  *state = *std::get<index>(cog_states_)->get_mutable_state();
  return jewels::success;
}

template <typename... Policies>
template <size_t index>
jewels::BinaryOutcome CogStates<Policies...>::get_state_handle(jewels::Out<RecordPtrType<index>> state_handle)
{
  if (!is_state_set<index>())
  {
    return jewels::failure;
  }
  *state_handle = std::get<index>(cog_states_)->get_record_ptr();
  return jewels::success;
}

template <typename... Policies>
template <size_t index>
jewels::BinaryOutcome CogStates<Policies...>::set_state_handle(RecordPtrType<index> state_handle)
{
  if (is_state_set<index>())
  {
    return jewels::failure;
  }
  std::get<index>(cog_states_) = std::make_shared<StateType<index>>(std::move(state_handle));
  return jewels::success;
}

template <typename... Policies>
template <size_t index>
jewels::BinaryOutcome CogStates<Policies...>::initialize_state(pinion::PublisherHandle publisher_handle)
  requires TappyType<SchemaType<index>>
{
  if (is_state_set<index>())
  {
    return jewels::failure;
  }
  auto record_ptr = std::make_shared<CogStateDataImpl<SchemaType<index>>>(std::move(publisher_handle));
  std::get<index>(cog_states_) = std::make_shared<StateType<index>>(std::move(record_ptr));
  return jewels::success;
}

template <typename... Policies>
template <size_t index>
jewels::BinaryOutcome CogStates<Policies...>::initialize_state()
  requires(!TappyType<SchemaType<index>>)
{
  if (is_state_set<index>())
  {
    return jewels::failure;
  }
  auto record_ptr = std::make_shared<CogStateDataImpl<SchemaType<index>>>(
    jewels::memory::MemoryResource{std::pmr::get_default_resource()});
  std::get<index>(cog_states_) = std::make_shared<StateType<index>>(std::move(record_ptr));
  return jewels::success;
}

} // namespace clockwork
