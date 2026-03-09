// IWYU pragma: private, include "clockwork/cog/simple_cog.hh"
#pragma once
#include "clockwork/cog/simple_cog.hh"

#include "clockwork/cog/cog_statistics.hh"
#include "clockwork/cog/interface.hh"
#include "clockwork/common/abstract_cog.hh"
#include "clockwork/common/abstract_cog_queue.hh"
#include "clockwork/common/cog_execution_error_clk_cc.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/container/compare.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/scope_guard/scope_guard.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace clockwork
{

static constexpr auto metrics_send_interval = std::chrono::seconds{1};

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
    inputs_(memory_resource_, queue->is_offline()),
    conditions_(memory_resource_),
    publishers_(memory_resource_),
    diagnostics_(instance_id_),
    infra_diagnostics_(instance_id_),
    metrics_(memory_resource_, Policy::event_metrics_batch_size)
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
SimpleCog<Policy>::set_handle(jewels::Uuid<common::EndpointClassId> uuid, std::shared_ptr<CogConfigData> config)
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
jewels::expected<void, jewels::MonoError> SimpleCog<Policy>::set_subscriber(jewels::Uuid<common::EndpointClassId> uuid)
{
  // Set up dummy condition and input for non-connected endpoint
  // The condition will always report as not ready, and the input will return empty views
  std::ignore = conditions_.set_condition(uuid);
  return inputs_.template set_input<SimpleCog<Policy>>(uuid);
}

template <typename Policy>
jewels::expected<void, jewels::MonoError> SimpleCog<Policy>::set_handle(
  jewels::Uuid<common::EndpointClassId> uuid, pinion::PublisherHandle&& handle, bool connected)
{
  if (diagnostics_.set_handle(uuid, std::move(handle)))
  {
    return {};
  }
  // NOLINTNEXTLINE(bugprone-use-after-move) It wasn't moved here if the call didn't succeed.
  if (infra_diagnostics_.set_handle(uuid, std::move(handle)))
  {
    return {};
  }
  // NOLINTNEXTLINE(bugprone-use-after-move) It wasn't moved here if the call didn't succeed.
  if (publishers_.set_handle(uuid, std::move(handle), connected))
  {
    return {};
  }
  // NOLINTNEXTLINE(bugprone-use-after-move) It wasn't moved here if the call didn't succeed.
  if (jewels::ok(states_.set_publisher_handle(uuid, std::move(handle))))
  {
    return {};
  }
  // NOLINTNEXTLINE(bugprone-use-after-move) It wasn't moved here if the call didn't succeed.
  if (jewels::ok(configs_.set_publisher_handle(uuid, std::move(handle))))
  {
    return {};
  }
  jewels::log_cerr_error("Unknown publisher endpoint '{}'", uuid);
  return jewels::unexpected(jewels::MonoError{});
}

template <typename Policy>
jewels::BinaryOutcome SimpleCog<Policy>::set_snapshot_config(
  jewels::Uuid<common::EndpointClassId> uuid, const Tappy<common::SnapshotConfig>& snapshot_config)
{
  // Determine if this is for a state (TakeSnapshots) or config (SnapshotOnce) endpoint
  // TakeSnapshots has at least one of interval or cycles set
  // SnapshotOnce has both optional fields empty
  const bool is_state_snapshot = snapshot_config.has_interval() || snapshot_config.has_cycles();

  if (is_state_snapshot)
  {
    // TakeSnapshots - route to states
    return states_.set_snapshot_config(uuid, snapshot_config);
  }

  // SnapshotOnce - route to configs and publish immediately
  return configs_.publish_snapshot(uuid);
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
      // The cog is currently executing. Check if it's in danger of being
      // overrun and terminate it if necessary.
      // TODO(OI-3250): Consider less disruptive approach than process termination, such as only killing the thread
      // that's currently executing the cog and blocking further executions of the cog by other threads.

      if (inputs_.almost_overrun())
      {
        jewels::log_cerr_error("Cog '{}' is dangerously close to being overrun. Terminating.", get_name());
        // In unit tests for this code, we override the terminate handler. So,
        // instead of aborting the entire process we pass control back to the
        // test harness.  If we hang onto this lock in that context, the cog
        // execution thread can't make progress and the test hangs while trying
        // to join it. To avoid this lock up we release the notification lock
        // before terminating. This shouldn't be a problem outside of tests
        // since the entire process is going down immediately after we unlock
        // anyway.
        notify_guard.unlock();
        std::terminate();
      }

      // No risk of an overrun. The holder of the reentry mutex is responsible for processing pending notifies after
      // they unlock
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
    if (is_ready)
    {
      metrics_.cog_ready(jewels::time::SyncClock::now());
    }
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
    // Ignore the error. If this fails, the metrics api logs the error to the console. No need to log it again here.
    std::ignore = metrics_.execution_attempted(jewels::time::SyncClock::now());
    reentry_mutex_.unlock();
    return jewels::unexpected(CogExecutionError::states_lock_contention);
  }

  // Check if the cog is ready.

  auto timer_conditions = timers_.make_conditions(current_time);
  auto input_conditions = conditions_.make_conditions();
  publishers_.update_rate_limiters(current_time);

  if (!Policy::is_ready(statistics_, timer_conditions, input_conditions) || publishers_.any_throttled())
  {
    states_.unlock();
    reentry_mutex_.unlock();
    return jewels::unexpected(CogExecutionError::not_ready);
  }
  // Ignore the error. If this fails, the metrics api logs the error to the console. No need to log it again here.
  std::ignore = metrics_.execution_attempted(jewels::time::SyncClock::now());

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

  // Make sure that all published once inputs have only been published once
  if (inputs_.is_published_once_channel_invalid())
  {
    jewels::log_cerr_error(
      "Published once channel has been published more than once in cog '{}'. Terminating.", get_name());
    std::terminate();
  }

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

  auto inputs = inputs_.template make_dial_inputs<ConditionsType>(*prepared_input_conditions_, params.start_time);

  if (!inputs)
  {
    return jewels::unexpected(CogExecutionError::make_inputs_failed);
  }

  // Create outputs

  // Make sure any channels that are only published once have never been published
  if (!publishers_.validate_published_once_outputs())
  {
    jewels::log_cerr_error("Published once outputs for cog '{}' have already been published. Terminating.", get_name());
    std::terminate();
  }

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
    *publishables,
    *prepared_timer_conditions_,
    *prepared_input_conditions_,
    diagnostics,
    signals_);

  if (!timers_.update_last_exec_time(params.start_time, *prepared_timer_conditions_))
  {
    // Timers are documented as not expected to fail; this would be an unexpected kernel failure.
    jewels::log_cerr_error("Timer re-arming failed");
    std::terminate();
  }

  // NOLINTNEXTLINE(misc-const-correctness) Because the mask can be set in the constexpr below it throws off the linter.
  uint64_t conditions_mask = 0;
  if constexpr (Policy::publish_metrics)
  {
    conditions_mask = Policy::get_conditions_mask(*prepared_timer_conditions_, *prepared_input_conditions_);
  }

  // Ignore the error. If this fails, the metrics api logs the error to the console. No need to log it again here.
  std::ignore = metrics_.execution_started(jewels::time::SyncClock::now(), conditions_mask);

  if constexpr (requires { Policy::start_of_execution_signals(signals_, params.start_time); })
  {
    Policy::start_of_execution_signals(signals_, params.start_time);
  }
  Policy::execute(dial);
  if constexpr (requires { Policy::end_of_execution_signals(signals_, params.start_time); })
  {
    Policy::end_of_execution_signals(signals_, params.start_time);
  }

  // For the online runner we need the actual time that this completed so this can't simply be passed in.
  auto exec_complete_time =
    (params.execution_mode == CogExecutionMode::deterministic ? params.start_time + Policy::simulated_execution_duration
                                                              : jewels::time::SyncClock::now());

  // Take state snapshots if configured (must happen before states_.unlock())
  if (jewels::fails(states_.publish_snapshots(exec_complete_time)))
  {
    // Snapshots are best-effort, log and continue
    jewels::log_cerr_error("State snapshot failures occurred in cog '{}'", get_name());
  }

  publishers_.update_throttle_status(*slots);

  // Ignore the error. If this fails, the metrics api logs the error to the console. No need to log it again here.
  std::ignore = metrics_.execution_completed(jewels::time::SyncClock::now());

  // Update output metrics for each publishable
  if constexpr (Policy::publish_metrics)
  {
    update_output_metrics(*publishables);
  }

  // Update stats
  auto is_overrun = inputs_.is_overrun();
  statistics_.on_execute_complete(is_overrun);

  // Check for inputs overruns


  if (is_overrun)
  {
    jewels::log_cerr_error("Execution overrun detected in cog '{}'. Terminating.", get_name());
    std::terminate();
  }
  else
  {
    conditions_.commit(inputs_.commit(*inputs));
    diagnostics_.commit(diagnostics, exec_complete_time);

    publish_metrics(publishables, params.start_time);
    if (const auto result = Policy::publish_report_groups(signals_, *publishables); jewels::fails(result))
    {
      jewels::log_cerr_error("Failed to publish report groups in cog '{}'", get_name());
    }

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

template <typename Policy>
template <typename PublishablesType>
void SimpleCog<Policy>::publish_metrics(PublishablesType& publishables, jewels::time::SyncTime execution_start_time)
{
  if constexpr (Policy::publish_metrics)
  {
    // Use the start time passed in rather than the result of SyncTime::now() so this is published reliable in sim and
    // testing.
    if (should_send_telemetry_metrics(execution_start_time))
    {
      Policy::populate_telemetry_metrics(metrics_.telemetry_metrics(), inputs_.subscribers(), *publishables);
      metrics_.reset_telemetry_metrics();
    }
    if (should_send_event_metrics(execution_start_time))
    {
      Policy::populate_event_metrics(metrics_.event_metrics(), inputs_.subscribers(), *publishables);
      metrics_.reset_event_metrics();
    }
  }
}

template <typename Policy>
bool SimpleCog<Policy>::should_send_telemetry_metrics(jewels::time::SyncTime current_time)
{
  if (!last_telemetry_sent_time_)
  {
    last_telemetry_sent_time_ = current_time;
    return false;
  }
  auto time_since_last_telemetry = current_time - *last_telemetry_sent_time_;

  if (time_since_last_telemetry >= metrics_send_interval)
  {
    last_telemetry_sent_time_ = current_time;
    return true;
  }
  return false;
}

template <typename Policy>
bool SimpleCog<Policy>::should_send_event_metrics(jewels::time::SyncTime current_time)
{
  if (!last_event_sent_time_)
  {
    last_event_sent_time_ = current_time;
    return false;
  }
  auto time_since_last_event = current_time - *last_event_sent_time_;

  if (metrics_.is_event_metrics_batch_full() || time_since_last_event >= metrics_send_interval)
  {
    last_event_sent_time_ = current_time;
    return true;
  }
  return false;
}

template <typename Policy>
template <typename PublishablesType>
void SimpleCog<Policy>::update_output_metrics(PublishablesType& publishables)
{
  [this, &publishables]<size_t... indices>(std::index_sequence<indices...>)
  {
    ((
       [this, &publishables]<size_t index>()
       {
         if constexpr (index != Policy::telemetry_metrics_index && index != Policy::event_metrics_index)
         {
           metrics_.update_output_metrics(index, std::get<index>(publishables).is_marked_for_publish() ? 1 : 0);
         }
       }.template operator()<indices>()),
     ...);
  }(std::make_index_sequence<std::tuple_size_v<std::remove_reference_t<PublishablesType>>>{});
}

} // namespace clockwork
