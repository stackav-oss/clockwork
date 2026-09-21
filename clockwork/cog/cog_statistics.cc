// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/cog_statistics.hh"

#include "clockwork/dsl/cog/ten_nanosecond_type.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/conversions.hh"
#include "jewels/time/sync_time.hh"

#include <wise_enum.h>

#include <algorithm>
#include <chrono>
#include <limits>
#include <string_view>
#include <tuple>
#include <utility>

// Collecting the cog latency measurements fulfulls the following requirements:

























namespace clockwork
{

void CogStatistics::on_execute_complete(bool is_overrun)
{
  num_executions_++;
  if (is_overrun)
  {
    num_overrun_++;
  }
}

CogMetrics::CogMetrics(jewels::memory::MemoryResource resource, size_t event_metrics_batch_size)
  : memory_resource_{std::move(resource)},
    event_metrics_batch_size_(event_metrics_batch_size),
    event_metrics_{memory_resource_},
    telemetry_metrics_{
      .output_metrics{memory_resource_},
      .conditions_mask_vector{memory_resource_},
      .publisher_throttle_counts{memory_resource_}},
    output_metrics_map_{memory_resource_},
    resource_metrics_map_{memory_resource_},
    publisher_throttle_active_{memory_resource_},
    publisher_throttle_counts_{memory_resource_}
{
  event_metrics_.reserve(event_metrics_batch_size_);
}

void CogMetrics::cog_ready(jewels::time::SyncTime ready_time)
{
  const std::scoped_lock lock{metrics_lock_};
  if (current_state_ == CogExecutionState::waiting_for_ready)
  {
    current_state_ = CogExecutionState::ready;
    first_ready_time_ = ready_time;
  }
}

StateTransitionExpected CogMetrics::execution_attempted(jewels::time::SyncTime attempt_time)
{
  const std::scoped_lock lock{metrics_lock_};

  if (current_state_ == CogExecutionState::ready)
  {
    num_requeues_before_execution_ = 0;
    first_attempt_time_ = attempt_time;
  }
  else if (current_state_ == CogExecutionState::execution_attempted)
  {
    ++num_requeues_before_execution_;
  }
  current_state_ = CogExecutionState::execution_attempted;
  return StateTransitionExpected{};
}

StateTransitionExpected CogMetrics::execution_started(
  jewels::time::SyncTime dial_start_time, jewels::time::SyncTime execution_time, uint64_t conditions_mask)
{
  const std::scoped_lock lock{metrics_lock_};
  if (current_state_ != CogExecutionState::execution_attempted)
  {
    print_invalid_transition_error(current_state_, CogExecutionState::execution_started);
    return jewels::unexpected(jewels::MonoError{});
  }
  current_state_ = CogExecutionState::execution_started;
  dial_start_time_ = dial_start_time;
  execution_start_time_ = execution_time;
  conditions_mask_ = conditions_mask;
  execute_cog_metrics_.reset();
  return StateTransitionExpected{};
}

StateTransitionExpected CogMetrics::execution_completed(jewels::time::SyncTime execution_complete_time)
{
  const std::scoped_lock lock{metrics_lock_};
  if (current_state_ != CogExecutionState::execution_started)
  {
    // Regardless of whether this is a valid state transition, transition back to the initial state so we can
    // potentially recover.
    current_state_ = CogExecutionState::waiting_for_ready;
    return jewels::unexpected(jewels::MonoError{});
  }
  current_state_ = CogExecutionState::waiting_for_ready;
  execution_complete_time_ = execution_complete_time;

  auto result = commit_metrics(lock);
  if (!result.has_value())
  {
    return jewels::unexpected(jewels::MonoError{});
  }
  return StateTransitionExpected{};
}

void CogMetrics::execute_cog_completed(const ExecuteCogMetrics& execute_cog_metrics)
{
  const std::scoped_lock lock{metrics_lock_};
  if (current_state_ != CogExecutionState::execution_started)
  {
    print_invalid_transition_error(current_state_, CogExecutionState::execution_started);
    return;
  }
  execute_cog_metrics_ = execute_cog_metrics;
}

std::pmr::vector<EventMetrics> CogMetrics::event_metrics() const
{
  const std::scoped_lock lock{metrics_lock_};
  return event_metrics_;
}

TelemetryMetrics CogMetrics::telemetry_metrics() const
{
  const std::scoped_lock lock{metrics_lock_};
  return telemetry_metrics_;
}

EventMetrics CogMetrics::current_execution_metrics() const
{
  const std::scoped_lock lock{metrics_lock_};
  EventMetrics result{
    .execute_cog_wall_duration = std::nullopt,
    .execute_cog_thread_cpu_duration = std::nullopt,
    .execute_cog_thread_user_duration = std::nullopt,
    .execute_cog_thread_system_duration = std::nullopt,
    .output_metrics{memory_resource_},
    .resource_metrics{memory_resource_},
    .publisher_throttle_counts{memory_resource_},
  };
  result.execution_start_time = jewels::time::get_ns(execution_start_time_);
  result.dial_start_time = jewels::time::get_ns(dial_start_time_);
  result.execution_duration = to_recorded_duration(execution_complete_time_, execution_start_time_);
  if (execute_cog_metrics_)
  {
    result.execute_cog_wall_duration = execute_cog_metrics_->wall_duration;
    result.execute_cog_thread_cpu_duration = execute_cog_metrics_->thread_cpu_duration;
    result.execute_cog_thread_user_duration = execute_cog_metrics_->thread_user_duration;
    result.execute_cog_thread_system_duration = execute_cog_metrics_->thread_system_duration;
  }
  result.latency_first_ready_to_execution = to_recorded_duration(execution_start_time_, first_ready_time_);
  result.latency_first_attempt_to_execution = to_recorded_duration(execution_start_time_, first_attempt_time_);
  result.num_requeues_before_execution = static_cast<uint16_t>(num_requeues_before_execution_);
  result.conditions_mask = conditions_mask_;
  for (const auto& [index, output_count] : output_metrics_map_)
  {
    result.output_metrics[index] = output_count;
  }
  for (const auto& [index, resource_metrics] : resource_metrics_map_)
  {
    result.resource_metrics[index] = resource_metrics;
  }
  for (const auto& [index, throttle_count] : publisher_throttle_counts_)
  {
    result.publisher_throttle_counts[index] = throttle_count;
  }
  if (publisher_throttle_first_rejection_time_)
  {
    result.was_publisher_throttled = true;
    result.publisher_throttle_wait_duration =
      to_recorded_duration(publisher_throttle_final_deadline_, *publisher_throttle_first_rejection_time_);
    result.post_throttle_exec_latency = to_recorded_duration(execution_start_time_, publisher_throttle_final_deadline_);
    result.last_throttled_until = jewels::time::get_ns(publisher_throttle_final_deadline_);
  }
  return result;
}

StateTransitionExpected CogMetrics::commit_metrics(const std::scoped_lock<std::mutex>& lock)
{
  auto event_metrics_update_success = update_cog_event_metrics(lock);
  if (!event_metrics_update_success.has_value())
  {
    return jewels::unexpected(jewels::MonoError{});
  }
  update_cog_telemetry_metrics();
  output_metrics_map_.clear();
  publisher_throttle_counts_.clear();
  publisher_throttle_first_rejection_time_.reset();
  publisher_throttle_final_deadline_ = {};
  previous_execution_start_time_.emplace(execution_start_time_);
  return StateTransitionExpected{};
}

void CogMetrics::reset_metrics()
{
  const std::scoped_lock lock{metrics_lock_};
  telemetry_metrics_ = TelemetryMetrics{
    .output_metrics{memory_resource_},
    .conditions_mask_vector{memory_resource_},
    .publisher_throttle_counts{memory_resource_}};
  event_metrics_.clear();
  output_metrics_map_.clear();
  execute_cog_metrics_.reset();
}

void CogMetrics::reset_telemetry_metrics()
{
  const std::scoped_lock lock{metrics_lock_};
  telemetry_metrics_ = TelemetryMetrics{
    .output_metrics{memory_resource_},
    .conditions_mask_vector{memory_resource_},
    .publisher_throttle_counts{memory_resource_}};
}

void CogMetrics::reset_event_metrics()
{
  const std::scoped_lock lock{metrics_lock_};
  event_metrics_.clear();
}

void CogMetrics::update_cog_telemetry_metrics()
{
  update_min_max_duration(execution_complete_time_, execution_start_time_, telemetry_metrics_.execution_duration);
  if (execute_cog_metrics_)
  {
    telemetry_metrics_.execute_cog_wall_duration.update(execute_cog_metrics_->wall_duration);
    telemetry_metrics_.execute_cog_thread_cpu_duration.update(execute_cog_metrics_->thread_cpu_duration);
    telemetry_metrics_.execute_cog_thread_user_duration.update(execute_cog_metrics_->thread_user_duration);
    telemetry_metrics_.execute_cog_thread_system_duration.update(execute_cog_metrics_->thread_system_duration);
  }
  update_min_max_duration(
    execution_start_time_, first_attempt_time_, telemetry_metrics_.latency_first_attempt_to_execution);
  update_min_max_duration(
    execution_start_time_, first_ready_time_, telemetry_metrics_.latency_first_ready_to_execution);
  // NOLINTNEXTLINE(cert-err33-c) False positive
  telemetry_metrics_.num_requeues_before_execution.update(static_cast<uint16_t>(num_requeues_before_execution_));
  if (previous_execution_start_time_)
  {
    update_min_max_duration(
      execution_start_time_, *previous_execution_start_time_, telemetry_metrics_.execution_period);
  }
  for (const auto& [index, output_count] : output_metrics_map_)
  {
    telemetry_metrics_.output_metrics[index].update(output_count); // NOLINT(cert-err33-c) False positive
  }
  if (publisher_throttle_first_rejection_time_)
  {
    ++telemetry_metrics_.throttled_execution_count;
    update_min_max_duration(
      publisher_throttle_final_deadline_,
      *publisher_throttle_first_rejection_time_,
      telemetry_metrics_.publisher_throttle_wait_duration);
    update_min_max_duration(
      execution_start_time_, publisher_throttle_final_deadline_, telemetry_metrics_.post_throttle_exec_latency);
  }
  for (const auto& [index, throttle_count] : publisher_throttle_counts_)
  {
    // MinMaxMean::update returns *this only to support chaining; there is no status to handle.
    std::ignore = telemetry_metrics_.publisher_throttle_counts[index].update(static_cast<uint64_t>(throttle_count));
  }
  ++telemetry_metrics_.num_executions;
}

StateTransitionExpected CogMetrics::update_cog_event_metrics(const std::scoped_lock<std::mutex>& lock)
{
  if (is_event_metrics_batch_full(lock))
  {
    return jewels::unexpected(jewels::MonoError{});
  }
  EventMetrics updated_metrics{
    .dial_start_time = jewels::time::get_ns(dial_start_time_),
    .execution_start_time = jewels::time::get_ns(execution_start_time_),
    .execute_cog_wall_duration = std::nullopt,
    .execute_cog_thread_cpu_duration = std::nullopt,
    .execute_cog_thread_user_duration = std::nullopt,
    .execute_cog_thread_system_duration = std::nullopt,
    .output_metrics{memory_resource_},
    .resource_metrics{memory_resource_},
    .publisher_throttle_counts{memory_resource_},
  };

  updated_metrics.execution_duration = to_recorded_duration(execution_complete_time_, execution_start_time_);
  if (execute_cog_metrics_)
  {
    updated_metrics.execute_cog_wall_duration = execute_cog_metrics_->wall_duration;
    updated_metrics.execute_cog_thread_cpu_duration = execute_cog_metrics_->thread_cpu_duration;
    updated_metrics.execute_cog_thread_user_duration = execute_cog_metrics_->thread_user_duration;
    updated_metrics.execute_cog_thread_system_duration = execute_cog_metrics_->thread_system_duration;
  }
  updated_metrics.latency_first_ready_to_execution = to_recorded_duration(execution_start_time_, first_ready_time_);
  updated_metrics.latency_first_attempt_to_execution = to_recorded_duration(execution_start_time_, first_attempt_time_);
  updated_metrics.num_requeues_before_execution = static_cast<uint16_t>(num_requeues_before_execution_);
  updated_metrics.conditions_mask = conditions_mask_;
  auto& event_metrics = event_metrics_.emplace_back(std::move(updated_metrics));
  for (const auto& [index, output_count] : output_metrics_map_)
  {
    event_metrics.output_metrics[index] = output_count;
  }
  for (const auto& [index, resource_metrics] : resource_metrics_map_)
  {
    event_metrics.resource_metrics[index] = resource_metrics;
  }
  for (const auto& [index, throttle_count] : publisher_throttle_counts_)
  {
    event_metrics.publisher_throttle_counts[index] = throttle_count;
  }
  if (publisher_throttle_first_rejection_time_)
  {
    event_metrics.was_publisher_throttled = true;
    event_metrics.publisher_throttle_wait_duration =
      to_recorded_duration(publisher_throttle_final_deadline_, *publisher_throttle_first_rejection_time_);
    event_metrics.post_throttle_exec_latency =
      to_recorded_duration(execution_start_time_, publisher_throttle_final_deadline_);
    event_metrics.last_throttled_until = jewels::time::get_ns(publisher_throttle_final_deadline_);
  }
  return StateTransitionExpected{};
}

size_t CogMetrics::current_event_metrics_batch_size() const
{
  const std::scoped_lock lock{metrics_lock_};
  return event_metrics_.size();
}

[[nodiscard]] bool CogMetrics::is_event_metrics_batch_full() const
{
  const std::scoped_lock lock{metrics_lock_};
  return event_metrics_.size() == event_metrics_batch_size_;
}

[[nodiscard]] bool CogMetrics::is_event_metrics_batch_full(const std::scoped_lock<std::mutex>& /*unused*/) const
{
  return event_metrics_.size() == event_metrics_batch_size_;
}

TenNanoseconds CogMetrics::to_recorded_duration(jewels::time::SyncTime end_time, jewels::time::SyncTime start_time)
{
  // Calculate duration in nanoseconds
  auto duration_ns = std::chrono::nanoseconds(jewels::time::get_ns(end_time) - jewels::time::get_ns(start_time));

  // Check for overflow before casting
  // Maximum representable value in TenNanoseconds (uint32_t max * 10ns)
  constexpr auto max_representable_ns = static_cast<int64_t>(std::numeric_limits<uint32_t>::max()) * 10;

  if (duration_ns.count() >= max_representable_ns)
  {
    // Return maximum representable value instead of wrapping
    return TenNanoseconds(std::numeric_limits<uint32_t>::max());
  }

  // Safe to cast now
  return std::chrono::duration_cast<TenNanoseconds>(duration_ns);
}

void CogMetrics::update_min_max_duration(
  jewels::time::SyncTime end_time, jewels::time::SyncTime start_time, MinMaxMean<TenNanoseconds>& min_max_duration)
{
  // Convert to TenNanoseconds, handling potential overflow
  const TenNanoseconds duration = to_recorded_duration(end_time, start_time);
  min_max_duration.update(duration); // NOLINT(cert-err33-c) False positive
}

void CogMetrics::print_invalid_transition_error(CogExecutionState from_state, CogExecutionState attempted_state)
{
  jewels::log_cerr_error(
    "Invalid cog metrics state transition from: {} to: {} ",
    wise_enum::to_string(from_state),
    wise_enum::to_string(attempted_state));
}

void CogMetrics::update_output_metrics(size_t output_index, uint16_t value)
{
  const std::scoped_lock lock{metrics_lock_};
  output_metrics_map_[output_index] = value;
}

void CogMetrics::update_resource_metrics(size_t resource_index, const jewels::memory::MemoryResourceMetrics& metrics)
{
  const std::scoped_lock lock{metrics_lock_};
  resource_metrics_map_[resource_index] = metrics;
}

void CogMetrics::update_publisher_throttle(
  const size_t publisher_index,
  const bool is_throttled,
  const jewels::time::SyncTime rejection_time,
  const jewels::time::SyncTime effective_deadline)
{
  const std::scoped_lock lock{metrics_lock_};
  auto& is_episode_active = publisher_throttle_active_[publisher_index];
  if (!is_throttled)
  {
    is_episode_active = false;
    return;
  }
  if (!is_episode_active)
  {
    is_episode_active = true;
    auto& throttle_count = publisher_throttle_counts_[publisher_index];
    if (throttle_count < std::numeric_limits<uint16_t>::max())
    {
      ++throttle_count;
    }
  }
  if (!publisher_throttle_first_rejection_time_)
  {
    publisher_throttle_first_rejection_time_ = rejection_time;
  }
  publisher_throttle_final_deadline_ = std::max(publisher_throttle_final_deadline_, effective_deadline);
}
} // namespace clockwork
