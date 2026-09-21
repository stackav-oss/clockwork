// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/cog/execute_cog_timing.hh"
#include "clockwork/dsl/cog/ten_nanosecond_type.hh"
#include "jewels/memory/instrumented_pmr_resource.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"

#include <wise_enum.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory_resource>
#include <mutex>
#include <optional>
#include <sys/types.h>
#include <unordered_map>
#include <vector>

namespace clockwork
{

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

  /// Get the sum of all observed values.
  /// @return The sum, or nullopt when no values have been observed.
  [[nodiscard]] std::optional<T> sum() const;

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
struct EventMetrics
{
  /// Timestamp passed to the cog dial at execution start
  int64_t dial_start_time{}; // 8 bytes (units 1ns signed)

  /// Start of execution time
  int64_t execution_start_time{}; // 8 bytes (units 1ns signed)

  /// Execution duration
  TenNanoseconds execution_duration{};

  /// Wall time spent in the generated cog execution body
  std::optional<TenNanoseconds> execute_cog_wall_duration;

  /// Thread CPU time spent in the generated cog execution body
  std::optional<TenNanoseconds> execute_cog_thread_cpu_duration;

  /// Thread user CPU time spent in the generated cog execution body
  std::optional<TenNanoseconds> execute_cog_thread_user_duration;

  /// Thread system CPU time spent in the generated cog execution body
  std::optional<TenNanoseconds> execute_cog_thread_system_duration;

  /// Latency between when the cog first became ready and when it actually executed
  TenNanoseconds latency_first_ready_to_execution{};

  /// Latency between when execution was first attempted (prepare for execution) and when it actually executed
  TenNanoseconds latency_first_attempt_to_execution{};

  /// Number of times the execution needed to be requeued.
  uint16_t num_requeues_before_execution{};

  /// Conditions Mask
  uint64_t conditions_mask{};

  /// Output metrics
  std::pmr::unordered_map<size_t, uint16_t> output_metrics;

  /// Memory resource metrics
  std::pmr::unordered_map<size_t, jewels::memory::MemoryResourceMetrics> resource_metrics;

  /// Publisher throttle episode counts by publisher index.
  std::pmr::unordered_map<size_t, uint16_t> publisher_throttle_counts;

  /// Wait from first publisher rejection through the final eligibility deadline.
  TenNanoseconds publisher_throttle_wait_duration{};

  /// Latency from the final publisher eligibility deadline to execution start.
  TenNanoseconds post_throttle_exec_latency{};

  /// Final publisher eligibility deadline as an absolute nanosecond timestamp.
  int64_t last_throttled_until{};

  /// Whether this execution experienced publisher throttling.
  bool was_publisher_throttled{};
};

/// Metrics common to all cogs that are written in the telemetry log.
struct TelemetryMetrics
{
  /// Execution Duration
  MinMaxMean<TenNanoseconds> latency_first_ready_to_execution{};

  /// Latency between when the cog first became ready and when it actually executed
  MinMaxMean<TenNanoseconds> latency_first_attempt_to_execution{};

  /// Number of times the execution needed to be requeued.
  MinMaxMean<uint16_t> num_requeues_before_execution{};

  /// Number of times this cog has been executed
  uint16_t num_executions{};

  /// Time between successive executions
  MinMaxMean<TenNanoseconds> execution_period{};

  /// Execution Duration
  MinMaxMean<TenNanoseconds> execution_duration{};

  /// Generated cog execution body wall-time duration
  MinMaxMean<TenNanoseconds> execute_cog_wall_duration{};

  /// Generated cog execution body thread CPU duration
  MinMaxMean<TenNanoseconds> execute_cog_thread_cpu_duration{};

  /// Generated cog execution body thread user CPU duration
  MinMaxMean<TenNanoseconds> execute_cog_thread_user_duration{};

  /// Generated cog execution body thread system CPU duration
  MinMaxMean<TenNanoseconds> execute_cog_thread_system_duration{};

  /// Output metrics
  std::pmr::unordered_map<size_t, MinMaxMean<uint16_t>> output_metrics;

  /// Conditions Mask Vector
  std::pmr::vector<uint64_t> conditions_mask_vector;

  /// Publisher throttle episode distributions by publisher index; sum is the telemetry count.
  std::pmr::unordered_map<size_t, MinMaxMean<uint64_t>> publisher_throttle_counts;

  /// Number of executions affected by publisher throttling.
  uint64_t throttled_execution_count{};

  /// Publisher throttle wait duration distribution.
  MinMaxMean<TenNanoseconds> publisher_throttle_wait_duration{};

  /// Post-throttle execution latency distribution.
  MinMaxMean<TenNanoseconds> post_throttle_exec_latency{};
};

/// CogExecution states. Should really be nested in CogMetrics but that would prevent using WISE_ENUM_CLASS
WISE_ENUM_CLASS((CogExecutionState, uint8_t), waiting_for_ready, ready, execution_attempted, execution_started)

using StateTransitionExpected = jewels::expected<void, jewels::MonoError>;
class CogMetrics
{
public:
  /// Constructor for the CogMetrics class
  /// @param resource Memory resource to use for event metrics storage
  explicit CogMetrics(jewels::memory::MemoryResource resource, size_t event_metrics_batch_size);

  /// Indicate that the cog's execution conditions have been satisfied
  /// @param ready_time the time the execution conditions were satisfied
  void cog_ready(jewels::time::SyncTime ready_time);

  /// Indicate that execution of the cog was attempted, regardless of whether execute was actually called
  /// @param attempt_time time execution was attempted
  /// @return A StateTransitionExpected indicating success or failure of the state transition
  StateTransitionExpected execution_attempted(jewels::time::SyncTime attempt_time);

  /// Indicate that execution of the cog began
  /// @param dial_start_time Timestamp passed to the cog dial for this execution.
  /// @param execution_time the time execution began
  /// @return A StateTransitionExpected indicating success or failure of the state transition
  StateTransitionExpected execution_started(
    jewels::time::SyncTime dial_start_time, jewels::time::SyncTime execution_time, uint64_t conditions_mask);

  /// Record durations from the generated cog execution body.
  void execute_cog_completed(const ExecuteCogMetrics& execute_cog_metrics);

  /// Indicate that the execution of the cog has completed
  /// @param execution_complete_time time execution of the cog completed
  /// @return A StateTransitionExpected indicating success or failure of the state transition
  StateTransitionExpected execution_completed(jewels::time::SyncTime execution_complete_time);

  /// Retrieve the collected event metrics for this cog
  /// @return A vector containing the event metrics history
  [[nodiscard]] std::pmr::vector<EventMetrics> event_metrics() const;

  /// Retrieve the aggregated telemetry metrics for this cog
  /// @return The current telemetry metrics
  [[nodiscard]] TelemetryMetrics telemetry_metrics() const;

  /// Reset all collected metrics to their initial values
  void reset_metrics();

  /// Reset the collected telemetry metrics to their initial values
  void reset_telemetry_metrics();

  /// Reset the collected event metrics
  void reset_event_metrics();

  // Get the current number of event metrics in the batch
  /// @return The current number of event metrics in the batch
  [[nodiscard]] size_t current_event_metrics_batch_size() const;

  // Check if the event metrics batch is full
  /// @return True if the event metrics batch is full, false otherwise
  [[nodiscard]] bool is_event_metrics_batch_full() const;

  /// Update the output metrics for a specific output index
  /// @param output_index The index of the output to update
  /// @param value The value to update the output metrics with
  void update_output_metrics(size_t output_index, uint16_t value);

  /// Update memory resource metrics for a specific memory resource index.
  /// @param resource_index The index of the memory resource to update
  /// @param metrics The MemoryResourceMetrics to update with
  void update_resource_metrics(size_t resource_index, const jewels::memory::MemoryResourceMetrics& metrics);

  /// Update publisher-throttle episode tracking for one publisher.
  /// @param[in] publisher_index Compile-time publisher policy index.
  /// @param[in] is_throttled Whether this publisher was rejected in the current check.
  /// @param[in] rejection_time Time of the rate-limiter check.
  /// @param[in] effective_deadline Effective Cog eligibility deadline for this failed check.
  void update_publisher_throttle(
    size_t publisher_index,
    bool is_throttled,
    jewels::time::SyncTime rejection_time,
    jewels::time::SyncTime effective_deadline);

  /// Construct a snapshot of the current execution's metrics from internal state.
  /// Unlike event_metrics() which returns the legacy batch, this reads directly
  /// from the current execution's state variables and the output_metrics_map_.
  /// Must be called after execution_completed() and update_output_metrics().
  /// @return An EventMetrics populated with the current execution's data.
  [[nodiscard]] EventMetrics current_execution_metrics() const;

private:
  /// Check if the event metrics batch is full
  /// @note Lock parameter is to ensure that the caller has locked the mutex
  /// @return True if the event metrics batch is full, false otherwise
  [[nodiscard]] bool is_event_metrics_batch_full(const std::scoped_lock<std::mutex>& /*unused*/) const;

  /// Commit the metrics from the last execution run to the event and telemtry metrics data structures
  /// @note Lock parameter is to ensure that the caller has locked the mutex
  StateTransitionExpected commit_metrics(const std::scoped_lock<std::mutex>& lock);

  /// Update the cog telemetry metrics with data from the last execution sequence
  void update_cog_telemetry_metrics();

  /// Update the cog event metrics with data from the last execution sequence
  /// @note Lock parameter is to ensure that the caller has locked the mutex
  StateTransitionExpected update_cog_event_metrics(const std::scoped_lock<std::mutex>& lock);

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

  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;

  /// Event metrics batch size
  size_t event_metrics_batch_size_;

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

  /// Timestamp passed to the cog dial when execution started
  jewels::time::SyncTime dial_start_time_{};

  /// Optional timestamp of the previous execution start time (used for period calculations)
  std::optional<jewels::time::SyncTime> previous_execution_start_time_{};

  /// Timestamp when execution completed
  jewels::time::SyncTime execution_complete_time_{};

  /// Conditions Mask
  uint64_t conditions_mask_{};

  /// Durations from the current generated cog execution body
  std::optional<ExecuteCogMetrics> execute_cog_metrics_;

  /// Collection of event metrics for batch processing
  std::pmr::vector<EventMetrics> event_metrics_;

  /// Aggregated telemetry metrics for this cog
  TelemetryMetrics telemetry_metrics_{};

  using OutputMetricsMap = std::pmr::unordered_map<size_t, uint16_t>;

  /// Output metrics map
  OutputMetricsMap output_metrics_map_;

  using MemoryResourceMetricsMap = std::pmr::unordered_map<size_t, jewels::memory::MemoryResourceMetrics>;

  /// Memory resource metrics map
  MemoryResourceMetricsMap resource_metrics_map_;

  using PublisherThrottleActiveMap = std::pmr::unordered_map<size_t, bool>;

  /// Active throttle episode state by publisher index.
  PublisherThrottleActiveMap publisher_throttle_active_;

  using PublisherThrottleCountMap = std::pmr::unordered_map<size_t, uint16_t>;

  /// Pending per-execution throttle episode counts by publisher index.
  PublisherThrottleCountMap publisher_throttle_counts_;

  /// First rejection time for the pending throttled execution.
  std::optional<jewels::time::SyncTime> publisher_throttle_first_rejection_time_;

  /// Final effective eligibility deadline for the pending throttled execution.
  jewels::time::SyncTime publisher_throttle_final_deadline_{};
};

struct InputMetrics
{
  uint16_t num_unseen_messages{};
  TenNanoseconds message_staleness{};
  uint16_t messages_dropped{};
};

struct InputEventMetrics
{
  uint16_t num_unseen_messages{};
  TenNanoseconds message_staleness{};
  uint16_t messages_dropped{};
};

struct InputTelemetryMetrics
{
  MinMaxMean<uint16_t> num_unseen_messages{};
  MinMaxMean<TenNanoseconds> message_staleness{};
  MinMaxMean<uint16_t> messages_dropped{};
};

struct AggregatedInputMetrics
{
  std::pmr::vector<InputEventMetrics> event_metrics;
  InputTelemetryMetrics telemetry_metrics{};
};
} // namespace clockwork

#include "clockwork/cog/cog_statistics.inl"
