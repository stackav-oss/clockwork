// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/include_common.hh"
#include "clockwork/dial/include_common.hh"
#include "clockwork/dsl/cog/ten_nanosecond_type.hh"
#include "clockwork/dsl/cog/tests/support/cog_metrics_test_cog_clk_cc.hh"
#include "clockwork/dsl/cog/tests/support/cog_metrics_test_cog_clk_cc_dial.hh"
#include "clockwork/dsl/cog/tests/support/mock_input_view.hh"
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/in_memory_channel.hh"
#include "clockwork/pinion/slot.hh"
#include "jewels/memory/instrumented_pmr_resource.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/time/sync_time.hh"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <memory_resource>
#include <optional>

namespace clockwork::testing::cogs
{

using Policy = CogMetricsTestCogPolicy;
using SignalApi = CogMetricsTestCogDialSignalApi;

// A fixed time reference used across tests.
constexpr auto test_time = jewels::time::SyncTime{std::chrono::nanoseconds{0}};

// A fixed execution period value used across tests.
constexpr auto test_period = TenNanoseconds{500};

constexpr auto output_channel_depth = 10U;

struct CogMetricsTelemetrySignalTestFixture
{
  using ResultPolicy = Policy::ResultPolicy;
  using ProcessingStatsPolicy = Policy::ProcessingStatsPolicy;
  using CogEventMetricsGroupPolicy = Policy::CogEventMetricsGroupPolicy;
  using CogTelemetryMetricsGroupPolicy = Policy::CogTelemetryMetricsGroupPolicy;

  CogMetricsTelemetrySignalTestFixture()
  {
    REQUIRE(publishers.set_handle(ResultPolicy::endpoint_id, result_channel.make_publisher(1), true));
    REQUIRE(
      publishers.set_handle(ProcessingStatsPolicy::endpoint_id, processing_stats_channel.make_publisher(1), true));
    REQUIRE(
      publishers.set_handle(CogEventMetricsGroupPolicy::endpoint_id, event_group_channel.make_publisher(1), true));
    REQUIRE(publishers.set_handle(
      CogTelemetryMetricsGroupPolicy::endpoint_id, telemetry_group_channel.make_publisher(1), true));
  }

  void populate(EventMetrics& event_metrics)
  {
    auto slots = publishers.reserve_slots();
    REQUIRE(slots);
    auto publishables = publishers.make_publishables(*slots);
    REQUIRE(publishables);
    Policy::populate_cog_metrics_signals(signals, event_metrics, inputs, *publishables, test_period, test_time);
  }

  jewels::memory::MemoryResource resource{std::pmr::new_delete_resource()};
  MockInputView<Policy::SensorAPolicy> sensor_a_view{resource};
  MockInputView<Policy::SensorBPolicy> sensor_b_view{resource};
  Policy::InputsType::SubscribersTuple inputs{
    std::shared_ptr<InputView<Policy::SensorAPolicy>>(&sensor_a_view, [](auto* /*p*/) {}),
    std::shared_ptr<InputView<Policy::SensorBPolicy>>(&sensor_b_view, [](auto* /*p*/) {})};
  InMemoryChannel<typename ResultPolicy::MsgType, output_channel_depth, false> result_channel{resource};
  InMemoryChannel<typename ProcessingStatsPolicy::MsgType, output_channel_depth, false> processing_stats_channel{
    resource};
  InMemoryChannel<typename CogEventMetricsGroupPolicy::MsgType, output_channel_depth, false> event_group_channel{
    resource};
  InMemoryChannel<typename CogTelemetryMetricsGroupPolicy::MsgType, output_channel_depth, false>
    telemetry_group_channel{resource};
  Policy::PublishersType publishers{resource};
  SignalApi signals{};
};

// The telemetry infra report group publishes this message type.
using TelemetryMsg = Policy::CogTelemetryMetricsGroupPolicy::MsgType;

// ===========================================================================
// populate_cog_metrics_signals telemetry value-correctness tests
//
// These tests verify that populate_cog_metrics_signals correctly maps
// EventMetrics fields to telemetry signal values.
//
// populate_cog_metrics_signals calls end_of_execution_cog_telemetry_metrics_group
// internally (incrementing the observation count). Values are then read back
// by calling populate_cog_telemetry_metrics_group to fill a TelemetryMsg.
// ===========================================================================

TEST_CASE_METHOD(
  CogMetricsTelemetrySignalTestFixture,
  "populate_maps_global_event_metrics_to_telemetry_signals",
  "[cog_metrics_signals][telemetry]")
{
  EventMetrics event_metrics{};
  event_metrics.execution_duration = TenNanoseconds{250};
  event_metrics.execute_cog_wall_duration = TenNanoseconds{240};
  event_metrics.execute_cog_thread_cpu_duration = TenNanoseconds{230};
  event_metrics.execute_cog_thread_user_duration = TenNanoseconds{220};
  event_metrics.execute_cog_thread_system_duration = TenNanoseconds{10};
  event_metrics.latency_first_ready_to_execution = TenNanoseconds{20};
  event_metrics.latency_first_attempt_to_execution = TenNanoseconds{10};
  event_metrics.num_requeues_before_execution = 3U;

  Policy::start_of_execution_signals(signals, test_time);
  populate(event_metrics);

  TelemetryMsg msg{};
  Policy::populate_cog_telemetry_metrics_group(signals, msg);

  // Global signals carry the agg_ prefix; each has min/max/mean post-aggregations.
  CHECK(msg.get_agg_cog_exec_duration_value_min() == TenNanoseconds{250});
  CHECK(msg.get_agg_cog_exec_duration_value_max() == TenNanoseconds{250});
  CHECK(msg.get_agg_execute_cog_wall_duration_value_min() == TenNanoseconds{240});
  CHECK(msg.get_agg_execute_cog_thread_cpu_duration_value_min() == TenNanoseconds{230});
  CHECK(msg.get_agg_execute_cog_thread_user_duration_value_min() == TenNanoseconds{220});
  CHECK(msg.get_agg_execute_cog_thread_system_duration_value_min() == TenNanoseconds{10});
  CHECK(msg.get_agg_cog_ready_to_exec_latency_value_min() == TenNanoseconds{20});
  CHECK(msg.get_agg_cog_attempt_to_exec_latency_value_min() == TenNanoseconds{10});
  CHECK(msg.get_agg_cog_requeue_count_value_min() == 3U);
  CHECK(msg.get_agg_cog_requeue_count_value_max() == 3U);
  CHECK(msg.get_execution_count() == 1);
}

TEST_CASE_METHOD(
  CogMetricsTelemetrySignalTestFixture, "populate_maps_cog_exec_period", "[cog_metrics_signals][telemetry]")
{
  EventMetrics event_metrics{};

  Policy::start_of_execution_signals(signals, test_time);
  populate(event_metrics);

  TelemetryMsg msg{};
  Policy::populate_cog_telemetry_metrics_group(signals, msg);

  CHECK(msg.get_cog_exec_period_value_min() == test_period);
  CHECK(msg.get_cog_exec_period_value_max() == test_period);
}

TEST_CASE_METHOD(
  CogMetricsTelemetrySignalTestFixture,
  "post_aggregation_tracks_min_and_max_across_executions",
  "[cog_metrics_signals][telemetry]")
{
  // Three executions with different execution durations.
  Policy::start_of_execution_signals(signals, test_time);

  for (const auto duration : {TenNanoseconds{100}, TenNanoseconds{200}, TenNanoseconds{300}})
  {
    EventMetrics event_metrics{};
    event_metrics.execution_duration = duration;
    populate(event_metrics);
  }

  TelemetryMsg msg{};
  Policy::populate_cog_telemetry_metrics_group(signals, msg);

  CHECK(msg.get_agg_cog_exec_duration_value_min() == TenNanoseconds{100});
  CHECK(msg.get_agg_cog_exec_duration_value_max() == TenNanoseconds{300});
  CHECK(msg.get_agg_cog_exec_duration_value_mean() == Catch::Approx(200.0f).epsilon(0.001f));
  CHECK(msg.get_execution_count() == 3);
}

TEST_CASE_METHOD(
  CogMetricsTelemetrySignalTestFixture,
  "populate_maps_input_metrics_to_telemetry_signals",
  "[cog_metrics_signals][telemetry]")
{
  AggregatedInputMetrics a_metrics{};
  a_metrics.event_metrics.push_back(
    InputEventMetrics{.num_unseen_messages = 4U, .message_staleness = TenNanoseconds{60}, .messages_dropped = 1U});
  sensor_a_view.set_input_metrics(a_metrics);

  AggregatedInputMetrics b_metrics{};
  b_metrics.event_metrics.push_back(
    InputEventMetrics{.num_unseen_messages = 0U, .message_staleness = TenNanoseconds{}, .messages_dropped = 0U});
  sensor_b_view.set_input_metrics(b_metrics);

  EventMetrics event_metrics{};
  Policy::start_of_execution_signals(signals, test_time);
  populate(event_metrics);

  TelemetryMsg msg{};
  Policy::populate_cog_telemetry_metrics_group(signals, msg);

  CHECK(msg.get_agg_sensor_a_unseen_messages_value_min() == 4U);
  CHECK(msg.get_agg_sensor_a_staleness_value_min() == TenNanoseconds{60});
  CHECK(msg.get_agg_sensor_a_dropped_messages_value_min() == 1U);
  CHECK(msg.get_agg_sensor_b_unseen_messages_value_min() == 0U);
  CHECK(msg.get_agg_sensor_b_staleness_value_min() == TenNanoseconds{});
}

TEST_CASE_METHOD(
  CogMetricsTelemetrySignalTestFixture,
  "populate_maps_empty_input_metrics_to_zero_in_telemetry",
  "[cog_metrics_signals][telemetry]")
{
  // No event_metrics entries set on either view — all per-input signals must default to zero.
  EventMetrics event_metrics{};
  Policy::start_of_execution_signals(signals, test_time);
  populate(event_metrics);

  TelemetryMsg msg{};
  Policy::populate_cog_telemetry_metrics_group(signals, msg);

  CHECK(msg.get_agg_sensor_a_unseen_messages_value_min() == 0U);
  CHECK(msg.get_agg_sensor_a_staleness_value_min() == TenNanoseconds{});
  CHECK(msg.get_agg_sensor_a_dropped_messages_value_min() == 0U);
}

TEST_CASE_METHOD(
  CogMetricsTelemetrySignalTestFixture,
  "populate_maps_output_metrics_to_telemetry_signals",
  "[cog_metrics_signals][telemetry]")
{
  EventMetrics event_metrics{};
  event_metrics.output_metrics.emplace(0, static_cast<uint16_t>(5));

  Policy::start_of_execution_signals(signals, test_time);
  populate(event_metrics);

  TelemetryMsg msg{};
  Policy::populate_cog_telemetry_metrics_group(signals, msg);

  CHECK(msg.get_agg_result_num_messages_value_min() == 5U);
  CHECK(msg.get_agg_result_num_messages_value_max() == 5U);
}

TEST_CASE_METHOD(
  CogMetricsTelemetrySignalTestFixture,
  "populate_maps_condition_mask_to_active_count",
  "[cog_metrics_signals][telemetry]")
{
  // bit 0 = periodic, bit 1 = new_msg (timer conditions come first in the mask)
  EventMetrics event_metrics{};
  event_metrics.conditions_mask = 1ULL << 0U; // only periodic is active

  Policy::start_of_execution_signals(signals, test_time);
  populate(event_metrics);

  TelemetryMsg msg{};
  Policy::populate_cog_telemetry_metrics_group(signals, msg);

  // periodic_active_count uses VALUE post-aggregation: stores the value from the most recent execution.
  CHECK(msg.get_periodic_active_count_value_value() == 1U);
  CHECK(msg.get_new_msg_active_count_value_value() == 0U);
}

TEST_CASE_METHOD(
  CogMetricsTelemetrySignalTestFixture,
  "telemetry_end_of_execution_called_by_populate",
  "[cog_metrics_signals][telemetry]")
{
  // After a single populate_cog_metrics_signals call the observation count must be 1.
  EventMetrics event_metrics{};
  Policy::start_of_execution_signals(signals, test_time);
  populate(event_metrics);

  CHECK(signals.get_execution_count_cog_telemetry_metrics_group() == 1);
}

TEST_CASE_METHOD(
  CogMetricsTelemetrySignalTestFixture,
  "telemetry_end_of_execution_excluded_from_aggregate_method",
  "[cog_metrics_signals][telemetry]")
{
  // end_of_execution_signals must not advance the telemetry group's observation count.
  // This ensures the infra group is only closed via populate_cog_metrics_signals.
  Policy::start_of_execution_signals(signals, test_time);
  Policy::end_of_execution_signals(signals, test_time);

  CHECK(signals.get_execution_count_cog_telemetry_metrics_group() == 0);
}

} // namespace clockwork::testing::cogs
