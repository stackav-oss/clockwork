// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/cog/cog_statistics.hh"
#include "clockwork/dsl/cog/common_cog_event_metrics_clk_cc.hh"
#include "clockwork/dsl/cog/common_cog_telemetry_metrics_clk_cc.hh"
#include "clockwork/dsl/cog/ten_nanosecond_type.hh"
#include "clockwork/repr_iface.hh"

#include <vector>

namespace clockwork
{
/// Populates a Tachyon MinMaxMean message from a C++ MinMaxMean struct.
/// @tparam CppType The underlying C++ data type in the source.
/// @tparam TachyonType The underlying Tachyon data type in the target.
/// @param source The source C++ MinMaxMean struct.
/// @param target The target Tachyon MinMaxMean message builder.
template <typename CppType, typename TachyonType>
inline void
populate_tachyon_min_max_mean(const MinMaxMean<CppType>& source, Tappy<cog::metrics::MinMaxMean<TachyonType>>& target);

/// Populates a Tachyon MinMaxMean message from a C++ MinMaxMean struct for TenNanoseconds type.
/// @param source The source C++ MinMaxMean<TenNanoseconds> struct.
/// @param target The target Tachyon MinMaxMean<TenNanoseconds> message builder.
inline void populate_tachyon_min_max_mean(
  const MinMaxMean<TenNanoseconds>& source, Tappy<cog::metrics::MinMaxMean<TenNanoseconds>>& target);

/// Sets common event metrics from a vector into a target batch type.
/// @tparam EventMetricsBatchType The type of the target batch message.
/// @param event_metrics The vector of common event metrics.
/// @param target The target batch message builder.
template <typename EventMetricsBatchType>
void set_common_event_metrics(const std::pmr::vector<EventMetrics>& event_metrics, EventMetricsBatchType& target);

/// Sets input channel event metrics into a Tachyon message.
/// @param input_metrics The source input channel event metrics.
/// @param target The target Tachyon InputChannelEventMetrics message builder.
void set_input_channel_event_metrics(
  const InputEventMetrics& input_metrics, Tap<Tachyon<cog::metrics::InputChannelEventMetrics>>& target);

/// Sets input channel telemetry metrics into a Tachyon message.
/// @param input_metrics The source input channel telemetry metrics.
/// @param target The target Tachyon InputChannelTelemetryMetrics message builder.
void set_input_channel_telemetry_metrics(
  const InputTelemetryMetrics& input_metrics, Tap<Tachyon<cog::metrics::InputChannelTelemetryMetrics>>& target);
} // namespace clockwork

#include "clockwork/cog/set_cog_metrics.inl"
