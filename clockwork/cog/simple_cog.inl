// IWYU pragma: private, include "clockwork/cog/simple_cog.hh"
#pragma once
#include "clockwork/cog/simple_cog.hh"

#include "clockwork/cog/cog_statistics.hh"
#include "clockwork/cog/interface.hh"
#include "clockwork/common/abstract_cog.hh"
#include "clockwork/common/cog_execution_error.hh"
#include "clockwork/common/forward.hh"
#include "clockwork/common/process_description.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/scope_guard/scope_guard.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <fmt10/format.h> // IWYU pragma: keep

#include <algorithm>
#include <chrono>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <utility>

namespace clockwork
{

template <typename Policy>
SimpleCog<Policy>::SimpleCog(
  jewels::memory::MemoryResource resource,
  const jewels::Uuid<common::CogInstanceId>& instance_id,
  jewels::memory::ObjectPtr<AbstractCogQueue> queue)
  : CogBase(queue),
    memory_resource_(std::move(resource)),
    instance_id_(instance_id),
    states_(memory_resource_),
    timers_(memory_resource_),
    inputs_(memory_resource_),
    conditions_(memory_resource_),
    publishers_(memory_resource_),
    diagnostics_(instance_id_)
{
}

template <typename Policy>
SimpleCog<Policy>::~SimpleCog() = default;

template <typename Policy>
std::string_view SimpleCog<Policy>::get_name() const
{
  return Policy::name;
}

template <typename Policy>
const jewels::Uuid<common::CogInstanceId>& SimpleCog<Policy>::get_instance_id() const
{
  return instance_id_;
}

template <typename Policy>
jewels::expected<void, jewels::MonoError> SimpleCog<Policy>::set_handle(
  jewels::Uuid<common::EndpointClassId> uuid, jewels::memory::MemoryResource memory_resource)
{
  return memory_resources_.set_handle(uuid, std::move(memory_resource));
}

template <typename Policy>
jewels::expected<void, jewels::MonoError>
SimpleCog<Policy>::set_handle(jewels::Uuid<common::EndpointClassId> uuid, std::shared_ptr<const CogConfigData> config)
{
  return configs_.set_handle(uuid, std::move(config));
}

template <typename Policy>
jewels::expected<void, jewels::MonoError> SimpleCog<Policy>::set_handle(
  jewels::Uuid<common::EndpointClassId> uuid, std::shared_ptr<CogStateData> state, bool is_shared)
{
  return states_.set_handle(uuid, std::move(state), is_shared, jewels::memory::make_non_null_from_ref(*this));
}

template <typename Policy>
jewels::expected<std::shared_ptr<pinion::Observer>, jewels::MonoError>
SimpleCog<Policy>::set_handle(jewels::Uuid<common::EndpointClassId> uuid, std::shared_ptr<AbstractTimer> timer)
{
  return timers_.set_handle(uuid, timer, jewels::memory::make_non_null_from_ref(*this));
}

template <typename Policy>
jewels::expected<std::shared_ptr<pinion::Observer>, jewels::MonoError>
SimpleCog<Policy>::set_handle(jewels::Uuid<common::EndpointClassId> uuid, pinion::SubscriberHandle handle)
{
  std::ignore = conditions_.set_handle(uuid, handle);
  return inputs_.set_handle(uuid, handle, jewels::memory::make_non_null_from_ref(*this));
}

template <typename Policy>
jewels::expected<void, jewels::MonoError>
SimpleCog<Policy>::set_handle(jewels::Uuid<common::EndpointClassId> uuid, pinion::PublisherHandle&& handle)
{
  if (diagnostics_.set_handle(uuid, std::move(handle)))
  {
    return {};
  }
  // NOLINTNEXTLINE(bugprone-use-after-move) It wasn't moved here if the call didn't succeed.
  return publishers_.set_handle(uuid, std::move(handle));
}

template <typename Policy>
jewels::expected<void, jewels::MonoError> SimpleCog<Policy>::validate()
{
  if (
    memory_resources_.validate() && configs_.validate() && timers_.validate() && inputs_.validate() &&
    conditions_.validate() && publishers_.validate() && states_.validate() && diagnostics_.validate())
  {
    return {};
  }

  return jewels::unexpected{jewels::MonoError{}};
}

template <typename Policy>
jewels::expected<void, jewels::MonoError> SimpleCog<Policy>::prime(jewels::time::SyncTime start_time)
{
  return timers_.prime(start_time);
}

template <typename Policy>
void SimpleCog<Policy>::notify(jewels::time::SyncTime current_time)
{
  std::unique_lock notify_guard(notify_mutex_);
  maybe_pending_notify_time_ = std::max(current_time, maybe_pending_notify_time_.value_or(current_time));
  process_pending_notifies(notify_guard);
}

template <typename Policy>
void SimpleCog<Policy>::process_pending_notifies(std::unique_lock<std::mutex>& notify_guard)
{
  while (true)
  {
    if (!reentry_mutex_.try_lock())
    {
      // The holder of the reentry mutex is responsible for processing pending notifies after they unlock
      return;
    }
    if (!maybe_pending_notify_time_)
    {
      reentry_mutex_.unlock();
      return;
    }
    const auto current_time = *maybe_pending_notify_time_;
    maybe_pending_notify_time_ = std::nullopt;
    notify_guard.unlock();

    auto timer_conditions = timers_.make_conditions(current_time);
    auto input_conditions = conditions_.make_conditions();

    const bool is_ready = Policy::is_ready(statistics_, timer_conditions, input_conditions);

    reentry_mutex_.unlock();

    // Add ourselves to the ready queue if needed.
    // Defer queue push until after reentry_mutex_.unlock because the queue pop will trigger acquire reentry_mutex_
    // while holding the queue lock.  Thus this could deadlock where one thread holds the queue lock and the other the
    // inputs lock.
    if (is_ready)
    {
      add_to_ready_queue(current_time);
    }
    notify_guard.lock();
  }
}

template <typename Policy>
jewels::expected<void, CogExecutionError> SimpleCog<Policy>::prepare_for_execution(jewels::time::SyncTime current_time)
{
  if (!reentry_mutex_.try_lock())
  {
    return jewels::unexpected(CogExecutionError::reentry_lock_contention);
  }

  // Attempt to acquire the state locks.

  if (!states_.try_lock())
  {
    reentry_mutex_.unlock();
    return jewels::unexpected(CogExecutionError::states_lock_contention);
  }

  // Check if the cog is ready.

  auto timer_conditions = timers_.make_conditions(current_time);
  auto input_conditions = conditions_.make_conditions();

  if (!Policy::is_ready(statistics_, timer_conditions, input_conditions))
  {
    states_.unlock();
    reentry_mutex_.unlock();
    return jewels::unexpected(CogExecutionError::not_ready);
  }

  // Store the conditions and release the inputs lock

  prepared_timer_conditions_ = std::move(timer_conditions);
  prepared_input_conditions_ = std::move(input_conditions);

  return {};
}

template <typename Policy>
jewels::expected<void, CogExecutionError> SimpleCog<Policy>::execute(CogExecuteParams params)
{
  // Exit guard to ensure all locks are released.

  auto guard = std::make_optional<jewels::ScopeGuard<std::function<void()>>>(
    [this]()
    {
      prepared_timer_conditions_.reset();
      prepared_input_conditions_.reset();
      states_.unlock();
      reentry_mutex_.unlock();
      std::unique_lock notify_guard(notify_mutex_);
      process_pending_notifies(notify_guard);
    });

  // Create resources

  auto resources = memory_resources_.make_memory_resources();

  // Create configs

  auto configs = configs_.make_configs();

  // Create states

  auto states = states_.make_states();

  // Create inputs

  if (!prepared_timer_conditions_ || !prepared_input_conditions_)
  {
    return jewels::unexpected(CogExecutionError::not_ready);
  }

  auto inputs = inputs_.template make_dial_inputs<ConditionsType>(*prepared_input_conditions_);

  if (!inputs)
  {
    return jewels::unexpected(CogExecutionError::make_inputs_failed);
  }

  // Create outputs

  auto slots = publishers_.reserve_slots();
  if (!slots)
  {
    return jewels::unexpected(CogExecutionError::reserve_slots_failed);
  }

  auto publishables = publishers_.make_publishables(*slots);
  if (!publishables)
  {
    return jewels::unexpected(CogExecutionError::make_publishables_failed);
  }

  auto diagnostics = diagnostics_.make_report(params.start_time);

  // Execute!

  auto dial = Policy::make_dial(
    params,
    resources,
    configs,
    states,
    *inputs,
    std::move(*publishables),
    *prepared_timer_conditions_,
    *prepared_input_conditions_,
    diagnostics);

  if (!timers_.update_last_exec_time(params.start_time, *prepared_timer_conditions_))
  {
    // Timers are documented as not expected to fail; this would be an unexpected kernel failure.
    jewels::log_cerr_error("Timer re-arming failed");
    std::terminate();
  }

  try
  {
    Policy::execute(dial);
  }
  catch (...)
  {
    // Assume no outputs are trustworthy if an exception is thrown.
    pinion::mark_slots_for_discard(std::span{*slots});
    throw;
  }

  // For the online runner we need the actual time that this completed so this can't simply be passed in.
  auto exec_complete_time =
    (params.execution_mode == CogExecutionMode::deterministic ? params.start_time + Policy::simulated_execution_duration
                                                              : jewels::time::SyncClock::now());
  // Update stats
  auto is_overrun = inputs_.is_overrun();
  statistics_.on_execute_complete(is_overrun);

  // Check for inputs overruns

  if (is_overrun)
  {
    pinion::mark_slots_for_discard(std::span{*slots});
    jewels::log_cerr_error("Execution overrun detected in cog '{}'", get_name());
  }
  else
  {
    conditions_.commit(inputs_.commit(*inputs));
    diagnostics_.commit(diagnostics, exec_complete_time);

    // Process all the outputs.

    if (const auto result = pinion::process_slots(std::span{*slots}, exec_complete_time); !result)
    {
      throw std::runtime_error{fmt::format("Failed to process slots: {}", result.error())};
    }
  }

  // The readiness check should be performed again after execution. In the event where a notification arrives while the
  // cog is running, it will not receive the notification. As such, the cog should re-check triggers.

  guard = {};
  notify(exec_complete_time);
  notify_ready_queue();

  return is_overrun ? jewels::unexpected(CogExecutionError::overrun) : jewels::expected<void, CogExecutionError>{};
}

} // namespace clockwork
