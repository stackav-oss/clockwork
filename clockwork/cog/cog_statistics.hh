// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/dsl/cog/ten_nanosecond_type.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"

#include <wise_enum.h>

#include <cstdint>
#include <memory_resource>
#include <mutex>
#include <optional>
#include <sys/types.h>
#include <vector>

namespace clockwork
{
static constexpr size_t event_metrics_batch_size = 10;

/// Class to hold execution data for a Cog
struct CogStatistics
{
  CogStatistics() = default;

  /// Update statistics after an execution
  /// @param is_overrun Whether this execution resulted in an overrun condition
  void on_execute_complete(bool is_overrun);

  /// Total number of times this cog has been executed
  uint64_t num_executions_{};

  /// Total number of times this cog has experienced execution overrun
  uint64_t num_overrun_{};
};

/// Helper class to keep track of the min, max and mean values
template <typename T>
class MinMaxMean
{
public:
  /// Add a value to compute the min max and mean over.
  MinMaxMean<T>& update(T value);

  /// Reset the Min Max mean to default values
  void clear();

  /// Get the current min.
  /// @return min
  [[nodiscard]] std::optional<T> min() const;

  /// Get the current max
  /// @return max value
  [[nodiscard]] std::optional<T> max() const;

  /// Get the current mean
  /// @return mean
  [[nodiscard]] std::optional<double> mean() const;

private:
  /// Current min value.
  T min_{};

  /// Current max value
  T max_{};

  /// Sum of all values
  T sum_{0};

  /// Number of values added.
  size_t count_{0};

  /// Have any values ever been added
  bool set_{false};
};

// Template specialization for TenNanoseconds
template <>
inline std::optional<double> MinMaxMean<TenNanoseconds>::mean() const;

/// Metrics common to all cogs that are written in the event log
struct CogEventMetrics
{
  /// Start of execution time
  int64_t execution_start_time; // 8 bytes (units 1ns signed)

  /// Execution duration
  TenNanoseconds execution_duration;

  /// Latency between when the cog first became ready and when it actually executed
  TenNanoseconds latency_first_ready_to_execution;

  /// Latency between when execution was first attempted (prepare for execution) and when it actually executed
  TenNanoseconds latency_first_attempt_to_execution;

  /// Number of times the execution needed to be requeued.
  uint16_t num_requeues_before_execution;
};

/// Metrics common to all cogs that are written in the telemetry log.
struct CogTelemetryMetrics
{
  /// Execution Duration
  MinMaxMean<TenNanoseconds> latency_first_ready_to_execution;

  /// Latency between when the cog first became ready and when it actually executed
  MinMaxMean<TenNanoseconds> latency_first_attempt_to_execution;

  /// Number of times the execution needed to be requeued.
  MinMaxMean<uint16_t> num_requeues_before_execution;

  /// Number of times this cog has been executed
  uint16_t num_executions{};

  /// Time between successive executions
  MinMaxMean<TenNanoseconds> execution_period;

  /// Execution Duration
  MinMaxMean<TenNanoseconds> execution_duration;
};

/// CogExecution states. Should really be nested in CogMetrics but that would prevent using WISE_ENUM_CLASS
WISE_ENUM_CLASS((CogExecutionState, uint8_t), waiting_for_ready, ready, execution_attempted, execution_started)

using StateTransitionExpected = jewels::expected<void, jewels::MonoError>;
class CogMetrics
{
public:
  /// Constructor for the CogMetrics class
  /// @param resource Memory resource to use for event metrics storage
  explicit CogMetrics(jewels::memory::MemoryResource resource);

  /// Indicate that the cog's execution conditions have been satisfied
  /// @param ready_time the time the execution conditions were satisfied
  void cog_ready(jewels::time::SyncTime ready_time);

  /// Indicate that execution of the cog was attempted, regardless of whether execute was actually called
  /// @param attempt_time time execution was attempted
  /// @return A StateTransitionExpected indicating success or failure of the state transition
  StateTransitionExpected execution_attempted(jewels::time::SyncTime attempt_time);

  /// Indicate that execution of the cog began
  /// @param execution_time the time execution began
  /// @return A StateTransitionExpected indicating success or failure of the state transition
  StateTransitionExpected execution_started(jewels::time::SyncTime execution_time);

  /// Indicate that the execution of the cog has completed
  /// @param execution_complete_time time execution of the cog completed
  /// @return A StateTransitionExpected indicating success or failure of the state transition
  StateTransitionExpected execution_completed(jewels::time::SyncTime execution_complete_time);

  /// Retrieve the collected event metrics for this cog
  /// @return A vector containing the event metrics history
  [[nodiscard]] std::pmr::vector<CogEventMetrics> event_metrics() const;

  /// Retrieve the aggregated telemetry metrics for this cog
  /// @return The current telemetry metrics
  [[nodiscard]] CogTelemetryMetrics telemetry_metrics() const;

  /// Reset all collected metrics to their initial values
  void reset_metrics();

  // Get the current number of event metrics in the batch
  /// @return The current number of event metrics in the batch
  [[nodiscard]] size_t current_event_metrics_batch_size() const;

  // Check if the event metrics batch is full
  /// @return True if the event metrics batch is full, false otherwise
  [[nodiscard]] bool is_event_metrics_batch_full() const;

private:
  /// Commit the metrics from the last execution run to the event and telemtry metrics data structures
  StateTransitionExpected commit_metrics();

  /// Update the cog telemetry metrics with data from the last execution sequence
  void update_cog_telemetry_metrics();

  /// Update the cog event metrics with data from the last execution sequence
  StateTransitionExpected update_cog_event_metrics();

  /// Take a end and start time of an event and convert it to a TenNanoseconds duration
  /// @param end_time end time of the event
  /// @param start_time start time of the event
  /// @return Duration in TenNanoseconds units
  static TenNanoseconds to_recorded_duration(jewels::time::SyncTime end_time, jewels::time::SyncTime start_time);

  /// Update the given MinMaxMean duration value based on the passed in event end and start time.
  /// @param end_time end time of the event.
  /// @param start_time start time of the event.
  /// @param min_max_duration the MinMaxMean duration value to be updated.
  static void update_min_max_duration(
    jewels::time::SyncTime end_time, jewels::time::SyncTime start_time, MinMaxMean<TenNanoseconds>& min_max_duration);

  /// Print an invalid transition error message
  /// @param from_state The current state
  /// @param attempted_state The invalid target state
  static void print_invalid_transition_error(CogExecutionState from_state, CogExecutionState attempted_state);

  /// Mutex for thread-safe access to metrics data
  mutable std::mutex metrics_lock_;

  /// Current execution state of the cog
  CogExecutionState current_state_ = CogExecutionState::waiting_for_ready;

  /// Timestamp when the cog first became ready for execution
  jewels::time::SyncTime first_ready_time_{};

  /// Timestamp when execution was first attempted
  jewels::time::SyncTime first_attempt_time_{};

  /// Number of times the cog was requeued before successful execution
  uint num_requeues_before_execution_{};

  /// Timestamp when execution started
  jewels::time::SyncTime execution_start_time_{};

  /// Optional timestamp of the previous execution start time (used for period calculations)
  std::optional<jewels::time::SyncTime> previous_execution_start_time_{};

  /// Timestamp when execution completed
  jewels::time::SyncTime execution_complete_time_{};

  /// Collection of event metrics for batch processing
  std::pmr::vector<CogEventMetrics> event_metrics_;

  /// Aggregated telemetry metrics for this cog
  CogTelemetryMetrics telemetry_metrics_{};
};

} // namespace clockwork

#include "clockwork/cog/cog_statistics.inl"
