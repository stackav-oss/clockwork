// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/cog_statistics.hh"

#include "clockwork/dsl/cog/ten_nanosecond_type.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/conversions.hh"
#include "jewels/time/sync_time.hh"

#include <wise_enum.h>

#include <chrono>
#include <limits>
#include <string_view>
#include <utility>

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

CogMetrics::CogMetrics(jewels::memory::MemoryResource resource)
  : event_metrics_{std::move(resource)}
{
  event_metrics_.reserve(event_metrics_batch_size);
};

void CogMetrics::cog_ready(jewels::time::SyncTime ready_time)
{
  const std::lock_guard<std::mutex> lock{metrics_lock_};
  if (current_state_ == CogExecutionState::waiting_for_ready)
  {
    current_state_ = CogExecutionState::ready;
    first_ready_time_ = ready_time;
  }
}

StateTransitionExpected CogMetrics::execution_attempted(jewels::time::SyncTime attempt_time)
{
  const std::lock_guard<std::mutex> lock{metrics_lock_};
  if ((current_state_ != CogExecutionState::ready) && (current_state_ != CogExecutionState::execution_attempted))
  {
    print_invalid_transition_error(current_state_, CogExecutionState::execution_attempted);
    return jewels::unexpected(jewels::MonoError{});
  }

  if (current_state_ == CogExecutionState::ready)
  {
    current_state_ = CogExecutionState::execution_attempted;
    num_requeues_before_execution_ = 0;
    first_attempt_time_ = attempt_time;
  }
  else if (current_state_ == CogExecutionState::execution_attempted)
  {
    ++num_requeues_before_execution_;
  }

  return StateTransitionExpected{};
}

StateTransitionExpected CogMetrics::execution_started(jewels::time::SyncTime execution_time)
{
  const std::lock_guard<std::mutex> lock{metrics_lock_};
  if (current_state_ != CogExecutionState::execution_attempted)
  {
    print_invalid_transition_error(current_state_, CogExecutionState::execution_started);
    return jewels::unexpected(jewels::MonoError{});
  }
  current_state_ = CogExecutionState::execution_started;
  execution_start_time_ = execution_time;
  return StateTransitionExpected{};
}

StateTransitionExpected CogMetrics::execution_completed(jewels::time::SyncTime execution_complete_time)
{
  const std::lock_guard<std::mutex> lock{metrics_lock_};
  if (current_state_ != CogExecutionState::execution_started)
  {
    // Regardless of whether this is a valid state transition, transition back to the initial state so we can
    // potentially recover.
    current_state_ = CogExecutionState::waiting_for_ready;
    return jewels::unexpected(jewels::MonoError{});
  }
  current_state_ = CogExecutionState::waiting_for_ready;
  execution_complete_time_ = execution_complete_time;

  auto result = commit_metrics();
  if (!result.has_value())
  {
    return jewels::unexpected(jewels::MonoError{});
  }
  return StateTransitionExpected{};
}

std::pmr::vector<CogEventMetrics> CogMetrics::event_metrics() const
{
  const std::lock_guard<std::mutex> lock{metrics_lock_};
  return event_metrics_;
}

CogTelemetryMetrics CogMetrics::telemetry_metrics() const
{
  const std::lock_guard<std::mutex> lock{metrics_lock_};
  return telemetry_metrics_;
}

StateTransitionExpected CogMetrics::commit_metrics()
{
  auto event_metrics_update_success = update_cog_event_metrics();
  if (!event_metrics_update_success.has_value())
  {
    return jewels::unexpected(jewels::MonoError{});
  }
  update_cog_telemetry_metrics();
  previous_execution_start_time_.emplace(execution_start_time_);
  return StateTransitionExpected{};
}

void CogMetrics::reset_metrics()
{
  const std::lock_guard<std::mutex> lock{metrics_lock_};
  telemetry_metrics_ = CogTelemetryMetrics{};
  event_metrics_.clear();
}

void CogMetrics::update_cog_telemetry_metrics()
{
  update_min_max_duration(execution_complete_time_, execution_start_time_, telemetry_metrics_.execution_duration);
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
  ++telemetry_metrics_.num_executions;
}

StateTransitionExpected CogMetrics::update_cog_event_metrics()
{
  if (is_event_metrics_batch_full())
  {
    jewels::log_cerr_error(
      "Cog event metrics size {} exceeds the maximum size {}. "
      "This indicates that the event metrics should have been serviced and published but were not. ",
      event_metrics_.size(),
      event_metrics_batch_size);
    return jewels::unexpected(jewels::MonoError{});
  }
  CogEventMetrics updated_metrics{};
  updated_metrics.execution_start_time = jewels::time::get_ns(execution_start_time_);

  updated_metrics.execution_duration = to_recorded_duration(execution_complete_time_, execution_start_time_);
  updated_metrics.latency_first_ready_to_execution = to_recorded_duration(execution_start_time_, first_ready_time_);
  updated_metrics.latency_first_attempt_to_execution = to_recorded_duration(execution_start_time_, first_attempt_time_);
  updated_metrics.num_requeues_before_execution = static_cast<uint16_t>(num_requeues_before_execution_);

  event_metrics_.emplace_back(updated_metrics);
  return StateTransitionExpected{};
}

size_t CogMetrics::current_event_metrics_batch_size() const
{
  const std::lock_guard<std::mutex> lock{metrics_lock_};
  return event_metrics_.size();
}

[[nodiscard]] bool CogMetrics::is_event_metrics_batch_full() const
{
  return event_metrics_.size() == event_metrics_batch_size;
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

} // namespace clockwork
