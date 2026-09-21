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

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <tuple>

namespace clockwork::testing::cogs
{

using Policy = CogMetricsTestCogPolicy;
using SignalApi = CogMetricsTestCogDialSignalApi;

// A fixed time reference used across tests.
constexpr auto test_time = jewels::time::SyncTime{std::chrono::nanoseconds{0}};

// A fixed execution period passed to populate_cog_metrics_signals (value is irrelevant for event tests).
constexpr auto test_period = TenNanoseconds{0};

constexpr auto output_channel_depth = 10U;

// Fixture: resource is declared first so it outlives the views that allocate from it.
// Members are destroyed in reverse-declaration order, ensuring resource outlives views.
// SubscribersTuple stores shared_ptrs, so we use no-op deleters to avoid deleting stack objects.
struct CogMetricsSignalTestFixture
{
  using ResultPolicy = Policy::ResultPolicy;
  using ProcessingStatsPolicy = Policy::ProcessingStatsPolicy;
  using CogEventMetricsGroupPolicy = Policy::CogEventMetricsGroupPolicy;
  using CogTelemetryMetricsGroupPolicy = Policy::CogTelemetryMetricsGroupPolicy;

  CogMetricsSignalTestFixture()
  {
    REQUIRE(publishers.set_handle(ResultPolicy::endpoint_id, result_channel.make_publisher(1), true));
    REQUIRE(
      publishers.set_handle(ProcessingStatsPolicy::endpoint_id, processing_stats_channel.make_publisher(1), true));
    REQUIRE(
      publishers.set_handle(CogEventMetricsGroupPolicy::endpoint_id, event_group_channel.make_publisher(1), true));
    REQUIRE(publishers.set_handle(
      CogTelemetryMetricsGroupPolicy::endpoint_id, telemetry_group_channel.make_publisher(1), true));
  }

  void populate(EventMetrics& event_metrics, size_t result_publish_count = 0U)
  {
    auto slots = publishers.reserve_slots();
    REQUIRE(slots);
    auto publishables = publishers.make_publishables(*slots);
    REQUIRE(publishables);
    if (result_publish_count > 0U)
    {
      std::get<0>(*publishables).mark_for_publish(result_publish_count);
    }
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

// ===========================================================================
// populate_cog_metrics_signals value-correctness tests
//
// These tests verify that populate_cog_metrics_signals correctly maps
// EventMetrics fields and input/output metrics to signal values.
//
// Because populate_cog_metrics_signals calls end_of_execution_cog_event_metrics_group
// internally (advancing the batch index from 0 to 1), the get_*() API reads
// from the new empty slot and cannot be used to retrieve the committed values.
// Instead, these tests use Policy::populate_cog_event_metrics_group to fill a
// batch message from the committed slot 0 and inspect the resulting fields.
// ===========================================================================

// The infra report group publishes this message type.
using BatchMsg = Policy::CogEventMetricsGroupPolicy::MsgType;

TEST_CASE_METHOD(
  CogMetricsSignalTestFixture, "populate_maps_global_event_metrics_to_signals", "[cog_metrics_signals][event]")
{
  EventMetrics event_metrics{};
  event_metrics.dial_start_time = 900'000'000LL;
  event_metrics.execution_start_time = 1'000'000'000LL;
  event_metrics.execution_duration = TenNanoseconds{100};
  event_metrics.execute_cog_wall_duration = TenNanoseconds{90};
  event_metrics.execute_cog_thread_cpu_duration = TenNanoseconds{80};
  event_metrics.execute_cog_thread_user_duration = TenNanoseconds{70};
  event_metrics.execute_cog_thread_system_duration = TenNanoseconds{10};
  event_metrics.latency_first_ready_to_execution = TenNanoseconds{10};
  event_metrics.latency_first_attempt_to_execution = TenNanoseconds{5};
  event_metrics.num_requeues_before_execution = 2U;

  Policy::start_of_execution_signals(signals, test_time);
  populate(event_metrics);

  BatchMsg batch_msg{};
  Policy::populate_cog_event_metrics_group(signals, batch_msg);

  REQUIRE(batch_msg.get_signals().size() == 1);
  const auto& entry = batch_msg.get_signals()[0];
  CHECK(entry.get_cog_dial_start_time_value() == jewels::time::SyncTime{std::chrono::nanoseconds{900'000'000LL}});
  CHECK(entry.get_cog_exec_start_time_value() == jewels::time::SyncTime{std::chrono::nanoseconds{1'000'000'000LL}});
  CHECK(entry.get_cog_exec_duration_value() == TenNanoseconds{100});
  CHECK(entry.get_execute_cog_wall_duration_value() == TenNanoseconds{90});
  CHECK(entry.get_execute_cog_thread_cpu_duration_value() == TenNanoseconds{80});
  CHECK(entry.get_execute_cog_thread_user_duration_value() == TenNanoseconds{70});
  CHECK(entry.get_execute_cog_thread_system_duration_value() == TenNanoseconds{10});
  CHECK(entry.get_cog_ready_to_exec_latency_value() == TenNanoseconds{10});
  CHECK(entry.get_cog_attempt_to_exec_latency_value() == TenNanoseconds{5});
  CHECK(entry.get_cog_requeue_count_value() == 2U);
}

TEST_CASE_METHOD(CogMetricsSignalTestFixture, "populate_maps_input_metrics_to_signals", "[cog_metrics_signals][event]")
{
  AggregatedInputMetrics a_metrics{};
  a_metrics.event_metrics.push_back(
    InputEventMetrics{.num_unseen_messages = 5U, .message_staleness = TenNanoseconds{30}, .messages_dropped = 2U});
  sensor_a_view.set_input_metrics(a_metrics);

  AggregatedInputMetrics b_metrics{};
  b_metrics.event_metrics.push_back(
    InputEventMetrics{.num_unseen_messages = 0U, .message_staleness = TenNanoseconds{}, .messages_dropped = 1U});
  sensor_b_view.set_input_metrics(b_metrics);

  EventMetrics event_metrics{};
  Policy::start_of_execution_signals(signals, test_time);
  populate(event_metrics);

  BatchMsg batch_msg{};
  Policy::populate_cog_event_metrics_group(signals, batch_msg);

  REQUIRE(batch_msg.get_signals().size() == 1);
  const auto& entry = batch_msg.get_signals()[0];
  CHECK(entry.get_sensor_a_unseen_messages_value() == 5U);
  CHECK(entry.get_sensor_a_staleness_value() == TenNanoseconds{30});
  CHECK(entry.get_sensor_a_dropped_messages_value() == 2U);
  CHECK(entry.get_sensor_b_unseen_messages_value() == 0U);
  CHECK(entry.get_sensor_b_staleness_value() == TenNanoseconds{});
  CHECK(entry.get_sensor_b_dropped_messages_value() == 1U);
}

TEST_CASE_METHOD(
  CogMetricsSignalTestFixture, "populate_maps_empty_input_metrics_to_zero", "[cog_metrics_signals][event]")
{
  // No event_metrics entries set on either view — all per-input signals must default to zero.
  EventMetrics event_metrics{};
  Policy::start_of_execution_signals(signals, test_time);
  populate(event_metrics);

  BatchMsg batch_msg{};
  Policy::populate_cog_event_metrics_group(signals, batch_msg);

  REQUIRE(batch_msg.get_signals().size() == 1);
  const auto& entry = batch_msg.get_signals()[0];
  CHECK(entry.get_sensor_a_unseen_messages_value() == 0U);
  CHECK(entry.get_sensor_a_staleness_value() == TenNanoseconds{});
  CHECK(entry.get_sensor_a_dropped_messages_value() == 0U);
}

TEST_CASE_METHOD(CogMetricsSignalTestFixture, "populate_maps_output_metrics_to_signals", "[cog_metrics_signals][event]")
{
  EventMetrics event_metrics{};
  event_metrics.output_metrics.emplace(0, static_cast<uint16_t>(7));

  Policy::start_of_execution_signals(signals, test_time);
  populate(event_metrics);

  BatchMsg batch_msg{};
  Policy::populate_cog_event_metrics_group(signals, batch_msg);

  REQUIRE(batch_msg.get_signals().size() == 1);
  CHECK(batch_msg.get_signals()[0].get_result_num_messages_value() == 7U);
}

TEST_CASE_METHOD(
  CogMetricsSignalTestFixture, "populate_sets_output_first_sequence_number", "[cog_metrics_signals][event]")
{
  EventMetrics event_metrics{};
  event_metrics.output_metrics.emplace(0, static_cast<uint16_t>(1));

  Policy::start_of_execution_signals(signals, test_time);
  populate(event_metrics, 1U);

  BatchMsg batch_msg{};
  Policy::populate_cog_event_metrics_group(signals, batch_msg);

  REQUIRE(batch_msg.get_signals().size() == 1);
  CHECK(batch_msg.get_signals()[0].get_result_first_sequence_number_value() == 0U);
}

TEST_CASE_METHOD(
  CogMetricsSignalTestFixture,
  "populate_sets_multi_message_output_first_sequence_number",
  "[cog_metrics_signals][event]")
{
  EventMetrics event_metrics{};
  event_metrics.output_metrics.emplace(0, static_cast<uint16_t>(2));

  Policy::start_of_execution_signals(signals, test_time);
  populate(event_metrics, 2U);

  BatchMsg batch_msg{};
  Policy::populate_cog_event_metrics_group(signals, batch_msg);

  REQUIRE(batch_msg.get_signals().size() == 1);
  const auto& signal = batch_msg.get_signals()[0];
  CHECK(signal.get_result_num_messages_value() == 2U);
  CHECK(signal.get_result_first_sequence_number_value() == 0U);
}

TEST_CASE_METHOD(
  CogMetricsSignalTestFixture, "populate_maps_absent_output_metrics_to_zero", "[cog_metrics_signals][event]")
{
  EventMetrics event_metrics{}; // No output_metrics entries.

  Policy::start_of_execution_signals(signals, test_time);
  populate(event_metrics);

  BatchMsg batch_msg{};
  Policy::populate_cog_event_metrics_group(signals, batch_msg);

  REQUIRE(batch_msg.get_signals().size() == 1);
  CHECK(batch_msg.get_signals()[0].get_result_num_messages_value() == 0U);
}

TEST_CASE_METHOD(CogMetricsSignalTestFixture, "populate_maps_condition_mask_to_signals", "[cog_metrics_signals][event]")
{
  EventMetrics event_metrics{};
  event_metrics.conditions_mask = 1ULL << 0U; // bit 0 = periodic; bit 1 = new_msg

  Policy::start_of_execution_signals(signals, test_time);
  populate(event_metrics);

  BatchMsg batch_msg{};
  Policy::populate_cog_event_metrics_group(signals, batch_msg);

  REQUIRE(batch_msg.get_signals().size() == 1);
  const auto& entry = batch_msg.get_signals()[0];
  CHECK(entry.get_periodic_active_value() == true);
  CHECK(entry.get_new_msg_active_value() == false);
}

} // namespace clockwork::testing::cogs
