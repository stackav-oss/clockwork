// IWYU pragma: private, include "clockwork/cog/simple_cog.hh"
#pragma once
#include "clockwork/cog/simple_cog.hh"

#include "clockwork/cog/cog_statistics.hh"
#include "clockwork/cog/cog_timers.hh"
#include "clockwork/cog/execute_cog_timing.hh"
#include "clockwork/cog/input_view.hh"
#include "clockwork/cog/interface.hh"
#include "clockwork/common/abstract_cog.hh"
#include "clockwork/common/abstract_cog_queue.hh"
#include "clockwork/common/cog_execution_error_clk_cc.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/dsl/cog/ten_nanosecond_type.hh"
#include "clockwork/pinion/publisher_slot_ref.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/container/compare.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/instrumented_pmr_resource.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/scope_guard/scope_guard.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/conversions.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
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
jewels::expected<std::shared_ptr<pinion::Observer>, jewels::MonoError> SimpleCog<Policy>::set_handle(
  jewels::Uuid<common::EndpointClassId> uuid, std::shared_ptr<pinion::AbstractChannel> channel)
{
  std::ignore = conditions_.set_handle(uuid, channel);
  return inputs_.set_handle(uuid, channel, jewels::memory::make_non_null_from_ref(*this));
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
CogPrepareOutcome SimpleCog<Policy>::prepare_for_execution(
  jewels::Out<jewels::time::SyncTime> throttled_until_out, const jewels::time::SyncTime current_time)
{
  if (!reentry_mutex_.try_lock())
  {
    return CogPrepareResult::reentry_lock_contention;
  }

  // Attempt to acquire the state locks.
  if (!states_.try_lock())
  {
    // Ignore the error. If this fails, the metrics api logs the error to the console. No need to log it again here.
    std::ignore = metrics_.execution_attempted(jewels::time::SyncClock::now());
    reentry_mutex_.unlock();
    return CogPrepareResult::states_lock_contention;
  }

  // Check if the cog is ready.

  auto timer_conditions = timers_.make_conditions(current_time);
  auto input_conditions = conditions_.make_conditions();

  if (!Policy::is_ready(statistics_, timer_conditions, input_conditions))
  {
    states_.unlock();
    reentry_mutex_.unlock();
    return CogPrepareResult::not_ready;
  }

  auto throttled_until = jewels::time::SyncTime::min();
  typename PublishersType::PublisherThrottleSet throttled_publishers;
  const auto rate_limit_result =
    publishers_.update_rate_limiters(jewels::Out{throttled_until}, jewels::Out{throttled_publishers}, current_time);
  for (size_t index = 0; index < PublishersType::policy_count; ++index)
  {
    metrics_.update_publisher_throttle(index, throttled_publishers.test(index), current_time, throttled_until);
  }
  if (jewels::fails(rate_limit_result))
  {
    if (throttled_until <= current_time)
    {
      jewels::log_cerr_error("Publisher rate limiter returned an elapsed deadline for cog '{}'.", get_name());
      std::terminate();
    }
    *throttled_until_out = throttled_until;
    states_.unlock();
    reentry_mutex_.unlock();
    return CogPrepareResult::publisher_throttled;
  }
  // Ignore the error. If this fails, the metrics api logs the error to the console. No need to log it again here.
  std::ignore = metrics_.execution_attempted(jewels::time::SyncClock::now());

  // Store the conditions and release the inputs lock

  prepared_timer_conditions_ = std::move(timer_conditions);
  prepared_input_conditions_ = std::move(input_conditions);

  return CogPrepareResult::ready;
}

template <typename Policy>
bool SimpleCog<Policy>::has_rate_limited_publishers() const
{
  return PublishersType::has_rate_limits;
}

template <typename Policy>
jewels::expected<void, CogExecutionError> SimpleCog<Policy>::execute(CogExecuteParams params)
{
  // Exit guard to ensure all locks are released.

  auto guard = std::make_optional(
    jewels::ScopeGuard{[this]()
                       {
                         prepared_timer_conditions_.reset();
                         prepared_input_conditions_.reset();
                         states_.unlock();
                         reentry_mutex_.unlock();
                         std::unique_lock notify_guard(notify_mutex_);
                         process_pending_notifies(notify_guard);
                       }});

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

  auto& timer_conditions = *prepared_timer_conditions_;
  auto& input_conditions = *prepared_input_conditions_;

  auto inputs = inputs_.template make_dial_inputs<ConditionsType>(input_conditions, params.start_time);

  if (!inputs)
  {
    return jewels::unexpected(CogExecutionError::make_inputs_failed);
  }

  if constexpr (requires { &Policy::resolve_alignment; })
  {
    if (handle_alignment_miss(*inputs, guard, params.start_time))
    {
      return {};
    }
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

  auto dial = make_dial_impl(
    params,
    resources,
    configs,
    states,
    *inputs,
    *publishables,
    timer_conditions,
    input_conditions,
    diagnostics,
    signals_);

  if (!timers_.update_last_exec_time(params.start_time, timer_conditions))
  {
    // Timers are documented as not expected to fail; this would be an unexpected kernel failure.
    jewels::log_cerr_error("Timer re-arming failed");
    std::terminate();
  }

  run_cog_execution(dial, params.start_time, timer_conditions, input_conditions);

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

  if constexpr (Policy::publish_metrics || requires { requires Policy::has_cog_metrics_report_groups; })
  {
    update_output_metrics(*publishables);
    update_resource_metrics(resources, states);
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
    if constexpr (requires { &Policy::commit_alignment; })
    {
      Policy::commit_alignment(inputs_, *inputs);
    }
    conditions_.commit(inputs_.commit(*inputs));
    diagnostics_.commit(diagnostics, exec_complete_time);

    publish_metrics(publishables, params.start_time);

    populate_cog_metrics_signals(*publishables, exec_complete_time);

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

  guard.reset();
  notify(exec_complete_time);
  notify_ready_queue();

  return is_overrun ? jewels::unexpected(CogExecutionError::overrun) : jewels::expected<void, CogExecutionError>{};
}

template <typename Policy>
template <typename InputDialTuple, typename GuardType>
bool SimpleCog<Policy>::handle_alignment_miss(
  InputDialTuple& inputs, GuardType& guard, jewels::time::SyncTime start_time)
  requires(requires { &Policy::resolve_alignment; })
{
  size_t stale_alignment_index{};
  const auto alignment_result = Policy::resolve_alignment(jewels::Out{stale_alignment_index}, inputs_, inputs);
  switch (alignment_result.get())
  {
  case clockwork::AlignedLookupResult::resolved:
    return false;

  case clockwork::AlignedLookupResult::stale:
  {
    jewels::log_cerr_warn("Stale alignment miss in cog '{}'. Skipping.", get_name());
    // Commit only the alignment channel to advance past stale messages.
    // Other input triggers are preserved.
    typename Policy::InputsType::LastViewedTuple commit_result;
    if (jewels::fails(inputs_.commit_single(jewels::Out{commit_result}, inputs, stale_alignment_index)))
    {
      jewels::log_cerr_error("Alignment resolution returned an invalid alignment input index.");
      // This is an invariant violation; termination is the safest option (UB may already have occurred)
      std::terminate();
    }
    const auto& [endpoint_id, last_viewed] = commit_result;
    conditions_.commit(endpoint_id, last_viewed);
    inputs_.reset_saved_state();
    guard.reset();
    notify(start_time);
    notify_ready_queue();
    return true;
  }

  case clockwork::AlignedLookupResult::pending:
  {
    jewels::log_cerr_debug("Pending alignment miss in cog '{}'. Will retry on data arrival.", get_name());
    // Don't commit anything. The alignment message stays "new".
    // Don't call notify() — it would unconditionally re-queue the cog because the
    // alignment message is still "new" (not committed), causing a busy-spin.
    // The scope guard drains any observer notifications that accumulated during
    // execution. Future upstream data re-wakes the cog via CogPassthroughObserver.
    inputs_.reset_saved_state();
    guard.reset();
    return true;
  }
  }
  __builtin_unreachable();
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
void SimpleCog<Policy>::populate_cog_metrics_signals(
  PublishablesType& publishables, jewels::time::SyncTime exec_complete_time)
{
  if constexpr (requires { requires Policy::has_cog_metrics_report_groups; })
  {
    auto current_event = metrics_.current_execution_metrics();
    auto exec_start = jewels::time::sync_time_from_ns(current_event.execution_start_time);
    // On the first execution, period is 0 because there's no previous start time.
    TenNanoseconds period{0};
    if (previous_exec_start_for_signals_)
    {
      auto delta_ns = exec_start - *previous_exec_start_for_signals_;
      // Clamp negative or overflowing deltas to the maximum representable TenNanoseconds value,
      // consistent with CogMetrics::to_recorded_duration().
      constexpr auto max_period = TenNanoseconds{std::numeric_limits<uint32_t>::max()};
      if (delta_ns.count() < 0 || delta_ns >= std::chrono::duration_cast<std::chrono::nanoseconds>(max_period))
      {
        period = max_period;
      }
      else
      {
        period = std::chrono::duration_cast<TenNanoseconds>(delta_ns);
      }
    }
    Policy::populate_cog_metrics_signals(
      signals_, current_event, inputs_.subscribers(), publishables, period, exec_complete_time);
    previous_exec_start_for_signals_ = exec_start;
  }
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
         // When publish_metrics is true, skip the internal metrics publisher slots
         // (telemetry + event) — they are not real outputs and would skew per-output counts.
         // When publish_metrics is false those slots do not exist on the policy at all,
         // so every index is a real output.

         if constexpr (Policy::publish_metrics)
         {
           if constexpr (index != Policy::telemetry_metrics_index && index != Policy::event_metrics_index)
           {
             const auto output_count = static_cast<uint16_t>(std::get<index>(publishables).get_metrics_publish_count());
             metrics_.update_output_metrics(index, output_count);
           }
         }
         else
         {
           const auto output_count = static_cast<uint16_t>(std::get<index>(publishables).get_metrics_publish_count());
           metrics_.update_output_metrics(index, output_count);
         }
       }.template operator()<indices>()),
     ...);
  }(std::make_index_sequence<std::tuple_size_v<std::remove_reference_t<PublishablesType>>>{});
}

template <typename Policy>
template <typename ResourcesType, typename StatesType>
void SimpleCog<Policy>::update_resource_metrics(ResourcesType& resources, StatesType& /*states*/)
{

  [this, &resources]<size_t... indices>(std::index_sequence<indices...>)
  {
    ((
       [this, &resources]<size_t index>()
       {
         jewels::memory::MemoryResourceMetrics resource_metrics;
         auto& resource = std::get<index>(resources);
         if (jewels::ok(resource.get_memory_resource_metrics(jewels::Out{resource_metrics})))
         {
           metrics_.update_resource_metrics(index, resource_metrics);
           resource.reset_incremental_metrics();
         }
       }.template operator()<indices>()),
     ...);
  }(std::make_index_sequence<std::tuple_size_v<std::remove_reference_t<ResourcesType>>>{});

  size_t metrics_index = std::tuple_size_v<std::remove_reference_t<ResourcesType>>;
  [this, &metrics_index]<size_t... indices>(std::index_sequence<indices...>)
  {
    ((
       [this, &metrics_index]<size_t index>()
       {
         if constexpr (requires {
                         std::declval<typename decltype(states_)::template RecordPtrType<index>::element_type&>()
                           .memres;
                       })
         {
           typename decltype(states_)::template RecordPtrType<index> state_handle;
           jewels::memory::MemoryResourceMetrics resource_metrics;
           if (
             jewels::ok(states_.template get_state_handle<index>(jewels::Out{state_handle})) &&
             jewels::ok(state_handle->memres.get_memory_resource_metrics(jewels::Out{resource_metrics})))
           {
             metrics_.update_resource_metrics(metrics_index++, resource_metrics);
           }
         }
       }.template operator()<indices>()),
     ...);
  }(std::make_index_sequence<std::tuple_size_v<std::remove_reference_t<StatesType>>>{});
}

template <typename Policy>
template <typename DialType, typename TimerConds, typename InputConds>
void SimpleCog<Policy>::run_cog_execution(
  DialType& dial, jewels::time::SyncTime start_time, TimerConds& timer_conditions, InputConds& input_conditions)
{
  const uint64_t conditions_mask = get_conditions_mask(timer_conditions, input_conditions);

  // Ignore the error. If this fails, the metrics api logs the error to the console. No need to log it again here.
  std::ignore = metrics_.execution_started(start_time, jewels::time::SyncClock::now(), conditions_mask);

  if constexpr (requires { Policy::start_of_execution_signals(signals_, start_time); })
  {
    Policy::start_of_execution_signals(signals_, start_time);
  }
  ExecuteCogTimingSnapshot execute_start;
  ExecuteCogTimingSnapshot execute_end;
  ExecuteCogMetrics execute_metrics;
  const auto timing_start_result =
    capture_execute_cog_timing(jewels::Out{execute_start}, jewels::time::SyncClock::now());
  Policy::execute(dial);
  const auto timing_end_result = capture_execute_cog_timing(jewels::Out{execute_end}, jewels::time::SyncClock::now());
  if (jewels::ok(timing_start_result) && jewels::ok(timing_end_result))
  {
    calculate_execute_cog_metrics(jewels::Out{execute_metrics}, execute_start, execute_end);
    metrics_.execute_cog_completed(execute_metrics);
  }
  else
  {
    jewels::log_cerr_error("Failed to capture generated cog execution CPU metrics for '{}'", get_name());
  }
  if constexpr (requires { Policy::end_of_execution_signals(signals_, start_time); })
  {
    Policy::end_of_execution_signals(signals_, start_time);
  }
}

template <typename Policy>
template <typename... DialArgs>
auto SimpleCog<Policy>::make_dial_impl(DialArgs&&... args)
{
  if constexpr (detail::has_dynamic_timer_v<Policy>)
  {
    auto& dynamic_timer_handler = timers_.template get_handler<Policy::dynamic_timer_index>();
    return Policy::make_dial(std::forward<DialArgs>(args)..., dynamic_timer_handler);
  }
  else
  {
    return Policy::make_dial(std::forward<DialArgs>(args)...);
  }
}

template <typename Policy>
template <typename TimerConds, typename InputConds>
uint64_t SimpleCog<Policy>::get_conditions_mask(
  const TimerConds& timer_conditions, const InputConds& input_conditions) const noexcept
{
  if constexpr (Policy::publish_metrics)
  {
    return Policy::get_conditions_mask(timer_conditions, input_conditions);
  }
  else if constexpr (
    // `requires { requires Expr; }` is needed here: the outer `requires` checks that the member
    // exists before evaluating it, preventing a hard error for policies that lack it entirely.
    requires { requires Policy::has_cog_metrics_report_groups; })
  {
    // Generic mask computation for signals-only cogs using is_active() on each condition handle.
    // Timer conditions occupy the low bits (0..N-1), input conditions occupy the next bits (N..N+M-1).
    static_assert(
      std::tuple_size_v<typename Policy::TimersType::ConditionsTuple> +
          std::tuple_size_v<typename Policy::ConditionsType::ConditionsTuple> <=
        std::numeric_limits<uint64_t>::digits,
      "Total number of conditions (timers + inputs) exceeds uint64_t capacity");
    uint64_t mask = 0;

    [&]<std::size_t... timer_bits>(std::index_sequence<timer_bits...>) noexcept
    {
      ((mask |= std::get<timer_bits>(timer_conditions).is_active() ? (uint64_t{1} << timer_bits) : uint64_t{0}), ...);
    }(std::make_index_sequence<std::tuple_size_v<typename Policy::TimersType::ConditionsTuple>>{});

    constexpr std::size_t timer_count = std::tuple_size_v<typename Policy::TimersType::ConditionsTuple>;
    [&]<std::size_t... cond_bits>(std::index_sequence<cond_bits...>) noexcept
    {
      ((mask |=
        std::get<cond_bits>(input_conditions).is_active() ? (uint64_t{1} << (timer_count + cond_bits)) : uint64_t{0}),
       ...);
    }(std::make_index_sequence<std::tuple_size_v<typename Policy::ConditionsType::ConditionsTuple>>{});

    return mask;
  }
  else
  {
    return 0;
  }
}
} // namespace clockwork
