// IWYU pragma: private, include "clockwork/cog/set_cog_metrics.hh"
#pragma once
#include "clockwork/cog/set_cog_metrics.hh"

#include "clockwork/cog/cog_statistics.hh"
#include "clockwork/dsl/cog/common_cog_telemetry_metrics_clk_cc.hh"
#include "clockwork/dsl/cog/ten_nanosecond_type.hh"
#include "clockwork/repr_iface.hh"

#include <chrono>
#include <cstdint>
#include <memory_resource>
#include <optional>
#include <vector>

namespace clockwork
{
template <typename CppType, typename TachyonType>
inline void
populate_tachyon_min_max_mean(const MinMaxMean<CppType>& source, Tappy<cog::metrics::MinMaxMean<TachyonType>>& target)
{
  if (auto opt_min = source.min(); opt_min)
  {
    target.set_min(*opt_min);
  }
  if (auto opt_max = source.max(); opt_max)
  {
    target.set_max(*opt_max);
  }
  if (auto opt_mean = source.mean(); opt_mean)
  {
    target.set_mean(static_cast<TachyonType>(*opt_mean));
  }
}

inline void populate_tachyon_min_max_mean(
  const MinMaxMean<TenNanoseconds>& source, Tappy<cog::metrics::MinMaxMean<TenNanoseconds>>& target)
{
  if (auto opt_min = source.min(); opt_min)
  {
    target.set_min(*opt_min);
  }
  if (auto opt_max = source.max(); opt_max)
  {
    target.set_max(*opt_max);
  }
  if (auto opt_mean = source.mean(); opt_mean)
  {
    target.set_mean(static_cast<TenNanoseconds>(static_cast<uint32_t>(*opt_mean)));
  }
}

template <typename CommonTelemetryMetricsType>
inline void set_common_telemetry_metrics(
  CommonTelemetryMetricsType& telemetry_metrics_tachyon, const TelemetryMetrics& telemetry_metrics)
{
  auto& common_telemetry_message = telemetry_metrics_tachyon.get_mutable_common_telemetry_metrics();

  common_telemetry_message.set_execution_count(telemetry_metrics.num_executions);
  populate_tachyon_min_max_mean(
    telemetry_metrics.execution_duration, common_telemetry_message.get_mutable_exec_duration());
  populate_tachyon_min_max_mean(
    telemetry_metrics.latency_first_ready_to_execution, common_telemetry_message.get_mutable_ready_to_exec_latency());

  populate_tachyon_min_max_mean(
    telemetry_metrics.latency_first_attempt_to_execution,
    common_telemetry_message.get_mutable_attempt_to_exec_latency());

  populate_tachyon_min_max_mean(
    telemetry_metrics.num_requeues_before_execution, common_telemetry_message.get_mutable_requeue_count());

  populate_tachyon_min_max_mean(telemetry_metrics.execution_period, common_telemetry_message.get_mutable_exec_period());
}

template <typename EventMetricsBatchType>
void set_common_event_metrics(const std::pmr::vector<EventMetrics>& event_metrics, EventMetricsBatchType& target)
{
  for (const auto& metric : event_metrics)
  {
    auto& target_metric = target.get_underlying_event_metrics().emplace_back().get_mutable_common_event_metrics();
    target_metric.set_exec_start_time(metric.execution_start_time);
    target_metric.set_dial_start_time(metric.dial_start_time);
    target_metric.set_exec_duration(metric.execution_duration);
    target_metric.set_ready_to_exec_latency(metric.latency_first_ready_to_execution);
    target_metric.set_attempt_to_exec_latency(metric.latency_first_attempt_to_execution);
    target_metric.set_requeue_count(metric.num_requeues_before_execution);
  }
}

} // namespace clockwork
