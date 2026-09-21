// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

// Integration tests verifying that SimpleCog correctly invokes the generated
// populate_cog_metrics_signals() code path during execution. These tests
// instantiate a real SimpleCog<CogMetricsTestCogPolicy> and drive
// it through the full execution lifecycle, then read the report-group output
// channels to verify the signals were populated and published.

#include "clockwork/cog/detail.hh"
#include "clockwork/cog/include_common.hh"
#include "clockwork/cog/simple_cog.hh"
#include "clockwork/common/abstract_timer.hh"
#include "clockwork/common/cog_envelope.hh"
#include "clockwork/dial/include_common.hh"
#include "clockwork/dsl/cog/tests/support/cog_metrics_test_cog_clk_cc.hh"
#include "clockwork/dsl/cog/tests/support/cog_metrics_test_cog_clk_cc_dial.hh"
#include "clockwork/dsl/tests/support/clk_hellomsg_clk_cc.hh"
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/buffer_layout.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/in_memory_channel.hh"
#include "clockwork/pinion/observer.hh"
#include "clockwork/pinion/publisher_slot_ref.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/slot_ref.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_shared_ptr.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/std/span.hh"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <memory_resource>
#include <ranges>
#include <span>

namespace clockwork::testing::cogs
{
namespace
{

using Policy = CogMetricsTestCogPolicy;
using TestCog = SimpleCog<Policy>;

// Report-group message types published by the signal framework.
using EventGroupMsg = Policy::CogEventMetricsGroupPolicy::MsgType;
using TelemetryGroupMsg = Policy::CogTelemetryMetricsGroupPolicy::MsgType;

// Input message type (Tap<Tachyon<HelloMsg>>).
using InputMsg = Policy::SensorAPolicy::MsgType;

// ---------------------------------------------------------------------------
// Minimal test doubles (same pattern as simple_cog_test.cc)
// ---------------------------------------------------------------------------

class TestTimer : public AbstractTimer
{
public:
  [[nodiscard]] int32_t descriptor() const override
  {
    return 0;
  }
  void set_observer(pinion::Observer* /*observer*/) override {}
  [[nodiscard]] bool start(jewels::time::SyncTime /*trigger_at*/, std::chrono::nanoseconds /*period*/) override
  {
    return true;
  }
  [[nodiscard]] bool stop() override
  {
    return true;
  }
  void notify(AbstractEPollManager& /*epoll*/, int /*efd*/, uint32_t /*events*/) override {}
};

class TestCogQueue : public AbstractCogQueue
{
public:
  void notify() override {}
  void push(CogEnvelope envelope) override
  {
    queue.push_back(envelope);
  }
  [[nodiscard]] PopResult pop(std::chrono::nanoseconds /*timeout*/) override
  {
    if (queue.empty())
    {
      return jewels::unexpected{jewels::MonoError{}};
    }
    auto result = queue.front();
    queue.pop_front();
    return result;
  }
  [[nodiscard]] CogQueueStats stats() const override
  {
    return {};
  }
  [[nodiscard]] bool is_offline() const override
  {
    return false;
  }

  std::deque<CogEnvelope> queue;
};

/// Deserialize a message from bytes into a writable buffer.
template <typename T>
void deserialize_message(T& msg, const auto& bytes)
{
  std::ranges::copy(bytes, std::as_writable_bytes(jewels::as_single_item_span(msg)).begin());
}

// ---------------------------------------------------------------------------
// Test fixture: real SimpleCog<CogMetricsTestCogPolicy> with channel wiring
// ---------------------------------------------------------------------------

/// Fixed timestamp used for test publishes.
inline constexpr auto k_publish_time = jewels::time::SyncTime{std::chrono::nanoseconds{12345}};

/// Channel depth used by all test channels.
inline constexpr auto k_channel_depth = 10U;

struct CogMetricsSignalsFixture
{
  using TimerPolicy = Policy::PeriodicPolicy;
  using SensorAPolicy = Policy::SensorAPolicy;
  using SensorBPolicy = Policy::SensorBPolicy;
  using ResultPolicy = Policy::ResultPolicy;
  using ProcessingStatsPolicy = Policy::ProcessingStatsPolicy;
  using CogEventMetricsGroupPolicy = Policy::CogEventMetricsGroupPolicy;
  using CogTelemetryMetricsGroupPolicy = Policy::CogTelemetryMetricsGroupPolicy;

  CogMetricsSignalsFixture()
    : resource(std::pmr::new_delete_resource()),
      instance_id(jewels::Uuid<common::CogInstanceId>::random_uuid()),
      cog(resource, instance_id, jewels::memory::make_non_null_from_ref(queue)),
      timer(std::make_shared<TestTimer>()),
      sensor_a_channel(std::make_shared<InMemoryChannel<InputMsg, k_channel_depth, false>>(resource)),
      sensor_a_publisher(sensor_a_channel->make_publisher(1)),
      sensor_b_channel(std::make_shared<InMemoryChannel<InputMsg, k_channel_depth, false>>(resource)),
      sensor_b_publisher(sensor_b_channel->make_publisher(1)),
      result_channel(resource),
      processing_stats_channel(resource),
      event_group_channel(resource),
      telemetry_group_channel(resource),
      result_subscriber(result_channel.make_subscriber()),
      event_group_subscriber(event_group_channel.make_subscriber()),
      telemetry_group_subscriber(telemetry_group_channel.make_subscriber())
  {
    wire_and_validate();
  }

  /// Publish a message on sensor_a (the input with a message condition).
  void publish_sensor_a()
  {
    auto slot = sensor_a_publisher.reserve().value();
    auto publishable = pinion::Publishable<InputMsg>::try_make(jewels::memory::make_non_null_from_ref(slot)).value();
    publishable.message() = InputMsg{};
    REQUIRE(slot.commit(k_publish_time));
  }

  /// Wire all endpoints and validate.  Initializes observers for timer and sensor_a.
  void wire_and_validate()
  {
    // Timer
    auto maybe_timer_observer = cog.set_handle(TimerPolicy::endpoint_id, timer);
    REQUIRE(maybe_timer_observer);
    timer_observer = *maybe_timer_observer;

    // Inputs (set_handle subscriber overload also sets up input conditions)
    auto maybe_sensor_a_observer = cog.set_handle(SensorAPolicy::endpoint_id, sensor_a_channel);
    REQUIRE(maybe_sensor_a_observer);
    sensor_a_observer = *maybe_sensor_a_observer;

    auto maybe_sensor_b_observer = cog.set_handle(SensorBPolicy::endpoint_id, sensor_b_channel);
    REQUIRE(maybe_sensor_b_observer);

    // Publisher outputs
    REQUIRE(cog.set_handle(ResultPolicy::endpoint_id, result_channel.make_publisher(1), true));
    REQUIRE(cog.set_handle(ProcessingStatsPolicy::endpoint_id, processing_stats_channel.make_publisher(1), true));
    REQUIRE(cog.set_handle(CogEventMetricsGroupPolicy::endpoint_id, event_group_channel.make_publisher(1), true));
    REQUIRE(
      cog.set_handle(CogTelemetryMetricsGroupPolicy::endpoint_id, telemetry_group_channel.make_publisher(1), true));

    REQUIRE(cog.validate());
  }

  /// Prepare the Cog for execution.
  void prepare(jewels::time::SyncTime exec_time)
  {
    auto throttled_until = jewels::time::SyncTime::min();
    REQUIRE(jewels::ok(cog.prepare_for_execution(jewels::Out{throttled_until}, exec_time)));
  }

  /// Trigger both conditions, prepare, and execute the cog at the given time.
  void run_one_execution(jewels::time::SyncTime exec_time)
  {
    timer_observer->notify({.current_time = exec_time});
    publish_sensor_a();
    sensor_a_observer->notify({.current_time = exec_time});

    prepare(exec_time);
    REQUIRE_NOTHROW(
      cog.execute(CogExecuteParams{.start_time = exec_time, .execution_mode = CogExecutionMode::deterministic}));
  }

  /// Trigger only the periodic timer condition (new_msg remains inactive).
  void run_timer_only_execution(jewels::time::SyncTime exec_time)
  {
    timer_observer->notify({.current_time = exec_time});

    prepare(exec_time);
    REQUIRE_NOTHROW(
      cog.execute(CogExecuteParams{.start_time = exec_time, .execution_mode = CogExecutionMode::deterministic}));
  }

  /// Trigger only the new_msg input condition (periodic timer remains inactive).
  void run_msg_only_execution(jewels::time::SyncTime exec_time)
  {
    publish_sensor_a();
    sensor_a_observer->notify({.current_time = exec_time});

    prepare(exec_time);
    REQUIRE_NOTHROW(
      cog.execute(CogExecuteParams{.start_time = exec_time, .execution_mode = CogExecutionMode::deterministic}));
  }

  // --- Members ---
  jewels::memory::MemoryResource resource;
  TestCogQueue queue;
  jewels::Uuid<common::CogInstanceId> instance_id;
  TestCog cog;

  std::shared_ptr<TestTimer> timer;

  std::shared_ptr<InMemoryChannel<InputMsg, k_channel_depth, false>> sensor_a_channel;
  pinion::PublisherHandle sensor_a_publisher;
  std::shared_ptr<InMemoryChannel<InputMsg, k_channel_depth, false>> sensor_b_channel;
  pinion::PublisherHandle sensor_b_publisher;

  InMemoryChannel<typename ResultPolicy::MsgType, k_channel_depth, false> result_channel;
  InMemoryChannel<typename ProcessingStatsPolicy::MsgType, k_channel_depth, false> processing_stats_channel;
  InMemoryChannel<typename CogEventMetricsGroupPolicy::MsgType, k_channel_depth, false> event_group_channel;
  InMemoryChannel<typename CogTelemetryMetricsGroupPolicy::MsgType, k_channel_depth, false> telemetry_group_channel;

  pinion::SubscriberHandle result_subscriber;
  pinion::SubscriberHandle event_group_subscriber;
  pinion::SubscriberHandle telemetry_group_subscriber;

  std::shared_ptr<pinion::Observer> timer_observer;
  std::shared_ptr<pinion::Observer> sensor_a_observer;
};

// ===========================================================================
// Tests
// ===========================================================================

TEST_CASE_METHOD(
  CogMetricsSignalsFixture,
  "SimpleCog publishes cog event metrics report group after batch fills",
  "[simple_cog_signals]")
{
  // The event batch size is 10 and the max window duration is 1 second.  Space
  // executions 50ms apart so all 10 fit within the window (450ms total), ensuring
  // the batch-full trigger fires rather than the time-window trigger.
  for (int idx = 0; idx < 10; ++idx)
  {
    REQUIRE(event_group_subscriber.available().empty());
    run_one_execution(jewels::time::SyncTime{std::chrono::milliseconds{1000 + (idx * 50)}});
  }

  // After 10 executions the event group should have been published.
  auto available = event_group_subscriber.available();
  REQUIRE_FALSE(available.empty());

  // Deserialize the first published message and verify basic fields.
  EventGroupMsg msg{};
  const auto& bytes = available.begin()->message();
  deserialize_message(msg, bytes);

  // Each entry should have a non-zero execution duration.
  const auto& first_entry = msg.get_signals()[0];
  CHECK(first_entry.get_cog_exec_duration_value().count() > 0);
}

TEST_CASE_METHOD(
  CogMetricsSignalsFixture,
  "SimpleCog publishes telemetry metrics report group after time window",
  "[simple_cog_signals]")
{
  using namespace std::chrono_literals;

  // The telemetry window max duration is 1 second (set in the generated signal API).
  // Execute at t=500ms to open the window, then at t=2000ms (1500ms later) to
  // force the window to close and the telemetry batch to be published.
  REQUIRE(telemetry_group_subscriber.available().empty());
  run_one_execution(jewels::time::SyncTime{500ms});
  REQUIRE(telemetry_group_subscriber.available().empty());
  run_one_execution(jewels::time::SyncTime{2000ms});

  auto available = telemetry_group_subscriber.available();
  REQUIRE_FALSE(available.empty());

  TelemetryGroupMsg msg{};
  const auto& bytes = available.begin()->message();
  deserialize_message(msg, bytes);
  CHECK(msg.get_execution_count() >= 1);
}

TEST_CASE_METHOD(
  CogMetricsSignalsFixture, "SimpleCog populates execution duration in event metrics signals", "[simple_cog_signals]")
{
  // Fill the batch (10 executions) so the event group gets published.  Use 50ms
  // spacing so all 10 executions fit inside the 1-second max-duration window
  // (450ms total), ensuring the batch-full trigger fires, not the time trigger.
  for (int idx = 0; idx < 10; ++idx)
  {
    REQUIRE(event_group_subscriber.available().empty());
    run_one_execution(jewels::time::SyncTime{std::chrono::milliseconds{1000 + (idx * 50)}});
  }

  auto available = event_group_subscriber.available();
  REQUIRE_FALSE(available.empty());

  EventGroupMsg msg{};
  const auto& bytes = available.begin()->message();
  deserialize_message(msg, bytes);

  // Every entry in the batch should have a positive execution duration.
  for (const auto& entry : msg.get_signals())
  {
    CHECK(entry.get_cog_exec_duration_value().count() > 0);
  }
}

TEST_CASE_METHOD(
  CogMetricsSignalsFixture,
  "SimpleCog populates condition active flags in event metrics signals",
  "[simple_cog_signals]")
{
  // Execute 10 times to fill the event batch.  Each execution is triggered by
  // both the periodic timer and the new_msg input condition, so both
  // periodic_active and new_msg_active must be true.  Use 50ms spacing so all
  // 10 executions fit inside the 1-second max-duration window (450ms total).
  for (int idx = 0; idx < 10; ++idx)
  {
    REQUIRE(event_group_subscriber.available().empty());
    run_one_execution(jewels::time::SyncTime{std::chrono::milliseconds{1000 + (idx * 50)}});
  }

  auto available = event_group_subscriber.available();
  REQUIRE_FALSE(available.empty());

  EventGroupMsg msg{};
  const auto& bytes = available.begin()->message();
  deserialize_message(msg, bytes);

  // Both conditions were active for every execution in this fixture.
  for (const auto& entry : msg.get_signals())
  {
    CHECK(entry.get_periodic_active_value() == true);
    CHECK(entry.get_new_msg_active_value() == true);
  }
}

TEST_CASE_METHOD(
  CogMetricsSignalsFixture,
  "SimpleCog populates condition active flags: periodic timer active, new_msg inactive",
  "[simple_cog_signals]")
{
  // Fire only the periodic timer for each execution; the new_msg condition is never triggered.
  // Use 50ms spacing so all 10 executions fit inside the 1-second max-duration window.
  for (int idx = 0; idx < 10; ++idx)
  {
    REQUIRE(event_group_subscriber.available().empty());
    run_timer_only_execution(jewels::time::SyncTime{std::chrono::milliseconds{1000 + (idx * 50)}});
  }

  auto available = event_group_subscriber.available();
  REQUIRE_FALSE(available.empty());

  EventGroupMsg msg{};
  const auto& bytes = available.begin()->message();
  deserialize_message(msg, bytes);

  for (const auto& entry : msg.get_signals())
  {
    CHECK(entry.get_periodic_active_value() == true);
    CHECK(entry.get_new_msg_active_value() == false);
  }
}

TEST_CASE_METHOD(
  CogMetricsSignalsFixture,
  "SimpleCog populates condition active flags: new_msg active, periodic timer inactive",
  "[simple_cog_signals]")
{
  // Publish a message for each execution; the periodic timer is never triggered.
  // Use 50ms spacing so all 10 executions fit inside the 1-second max-duration window.
  for (int idx = 0; idx < 10; ++idx)
  {
    REQUIRE(event_group_subscriber.available().empty());
    run_msg_only_execution(jewels::time::SyncTime{std::chrono::milliseconds{1000 + (idx * 50)}});
  }

  auto available = event_group_subscriber.available();
  REQUIRE_FALSE(available.empty());

  EventGroupMsg msg{};
  const auto& bytes = available.begin()->message();
  deserialize_message(msg, bytes);

  for (const auto& entry : msg.get_signals())
  {
    CHECK(entry.get_periodic_active_value() == false);
    CHECK(entry.get_new_msg_active_value() == true);
  }
}

TEST_CASE_METHOD(
  CogMetricsSignalsFixture, "SimpleCog publishes multiple event batches across many executions", "[simple_cog_signals]")
{
  // Execute 20 times (2x the batch size of 10) to verify
  // the signal framework publishes multiple batches.  Use 50ms spacing; the
  // first batch fills at 450ms, the second at 950ms — both within the 1-second
  // window, so both are flushed by the batch-full trigger.
  for (int idx = 0; idx < 20; ++idx)
  {
    run_one_execution(jewels::time::SyncTime{std::chrono::milliseconds{1000 + (idx * 50)}});
  }

  auto available = event_group_subscriber.available();
  // With a batch size of 10 and 20 executions, we expect at least 2 batches.
  CHECK(available.size() >= 2);
}

TEST_CASE_METHOD(
  CogMetricsSignalsFixture, "SimpleCog populates execution period in telemetry metrics signals", "[simple_cog_signals]")
{
  using namespace std::chrono_literals;

  // Three executions designed to exercise the period computation:
  //   exec 1: simulated start=1000ms → exec_complete=1001ms. First execution,
  //           so previous_exec_start_for_signals_ is nullopt → period=0.
  //   exec 2: simulated start=1100ms → exec_complete=1101ms. Still within the
  //           1-second telemetry window; period = 1100ms - 1000ms = 100ms.
  //   exec 3: simulated start=2500ms → exec_complete=2501ms. Period = 2500ms -
  //           1100ms = 1400ms. The gap from exec 1 (1001ms) exceeds 1s, so the
  //           telemetry window covering all three executions is published.
  REQUIRE(telemetry_group_subscriber.available().empty());
  run_one_execution(jewels::time::SyncTime{1000ms});
  run_one_execution(jewels::time::SyncTime{1100ms});
  REQUIRE(telemetry_group_subscriber.available().empty());
  run_one_execution(jewels::time::SyncTime{2500ms});

  auto available = telemetry_group_subscriber.available();
  REQUIRE_FALSE(available.empty());

  TelemetryGroupMsg msg{};
  deserialize_message(msg, available.begin()->message());

  // The published window covers all three executions.
  // exec 1 contributes period=0 (no prior start).
  // exec 2 and exec 3 contribute positive wall-clock periods (the period is
  // derived from CogMetrics::execution_start_time which uses SyncClock::now(),
  // so we cannot assert a deterministic simulated value here).
  CHECK(msg.get_cog_exec_period_value_min().count() == 0);
  CHECK(msg.get_cog_exec_period_value_max().count() > 0);
}

TEST_CASE_METHOD(
  CogMetricsSignalsFixture,
  "SimpleCog populates output_metrics for signals-only cogs (publish_metrics=false)",
  "[simple_cog_signals]")
{
  for (int idx = 0; idx < 10; ++idx)
  {
    run_one_execution(jewels::time::SyncTime{std::chrono::milliseconds{1000 + (idx * 50)}});
  }

  auto available = event_group_subscriber.available();
  REQUIRE_FALSE(available.empty());

  EventGroupMsg msg{};
  deserialize_message(msg, available.begin()->message());

  // execute_cog() calls mark_for_publish(1U) on the result publisher on every
  // execution, so update_output_metrics() records 1 for result at index 0.  Each
  // event-batch entry must therefore report exactly 1 result message published.
  uint64_t expected_sequence_number = 0U;
  for (const auto& entry : msg.get_signals())
  {
    CHECK(entry.get_result_num_messages_value() == 1);
    CHECK(entry.get_result_first_sequence_number_value() == expected_sequence_number);
    ++expected_sequence_number;
  }
}

TEST_CASE_METHOD(
  CogMetricsSignalsFixture,
  "SimpleCog reports per-execution unseen_messages correctly across mixed triggers",
  "[simple_cog_signals]")
{
  // First execution: only the periodic timer fires; sensor_a has no new messages.
  run_timer_only_execution(jewels::time::SyncTime{std::chrono::milliseconds{1000}});

  // Remaining 9 executions: sensor_a receives a new message each time.
  for (int idx = 1; idx < 10; ++idx)
  {
    run_msg_only_execution(jewels::time::SyncTime{std::chrono::milliseconds{1000 + (idx * 50)}});
  }

  auto available = event_group_subscriber.available();
  REQUIRE_FALSE(available.empty());

  EventGroupMsg msg{};
  deserialize_message(msg, available.begin()->message());

  const auto& signals = msg.get_signals();
  REQUIRE(signals.size() == 10);

  // Entry 0: periodic-only execution, no new message arrived on sensor_a.
  CHECK(signals[0].get_sensor_a_unseen_messages_value() == 0);
  CHECK(signals[0].get_periodic_active_value() == true);
  CHECK(signals[0].get_new_msg_active_value() == false);

  // Entries 1-9: message-triggered, exactly 1 new message per execution.
  for (size_t i = 1; i < signals.size(); ++i)
  {
    CHECK(signals[i].get_sensor_a_unseen_messages_value() == 1);
    CHECK(signals[i].get_new_msg_active_value() == true);
  }
}

// ===========================================================================
// Sequence number metadata tests
// ===========================================================================

TEST_CASE_METHOD(
  CogMetricsSignalsFixture, "SimpleCog populates sequence number metadata in event metrics", "[simple_cog_signals]")
{
  // Fill the event batch (10 executions). Each execution publishes one message
  // on sensor_a only, so sensor_a metadata should be populated and sensor_b
  // metadata should remain empty.
  for (int idx = 0; idx < 10; ++idx)
  {
    REQUIRE(event_group_subscriber.available().empty());
    run_one_execution(jewels::time::SyncTime{std::chrono::milliseconds{1000 + (idx * 50)}});
  }

  auto available = event_group_subscriber.available();
  REQUIRE_FALSE(available.empty());

  EventGroupMsg msg{};
  deserialize_message(msg, available.begin()->message());

  const auto& signals = msg.get_signals();
  REQUIRE(signals.size() >= 2);

  // Verify sensor_a metadata: the view grows to max_view_size=3, then slides
  // forward one sequence number at a time. The cursor position marks the first
  // new message, which is the latest message in this one-new-message-per-exec
  // sequence.
  for (size_t i = 0; i < signals.size(); ++i)
  {
    const auto& entry = signals[i];
    const auto& meta_a = entry.get_sensor_a_unseen_messages_value_metadata();
    auto seqnos = meta_a.get_message_sequence_numbers();
    const auto expected_size = std::min<size_t>(i + 1U, Policy::SensorAPolicy::max_view_size);
    REQUIRE(seqnos.size() == expected_size);
    CHECK(meta_a.get_cursor_position() == expected_size - 1U);

    const auto first_expected_seqno = static_cast<uint64_t>(i + 1U - expected_size);
    for (size_t seqno_idx = 0; seqno_idx < expected_size; ++seqno_idx)
    {
      CHECK(seqnos[seqno_idx] == first_expected_seqno + seqno_idx);
    }

    // sensor_b has no messages: metadata should be empty/zero.
    const auto& meta_b = entry.get_sensor_b_unseen_messages_value_metadata();
    CHECK(meta_b.get_message_sequence_numbers().empty());
    CHECK(meta_b.get_cursor_position() == 0U);
  }
}

TEST_CASE_METHOD(
  CogMetricsSignalsFixture,
  "SimpleCog populates sensor_b sequence number metadata when messages are published on sensor_b",
  "[simple_cog_signals]")
{
  // Publish a message on sensor_b before each execution (in addition to the
  // sensor_a publish that run_one_execution does). sensor_b should then have
  // non-empty metadata.
  for (int idx = 0; idx < 10; ++idx)
  {
    REQUIRE(event_group_subscriber.available().empty());
    // Publish on sensor_b
    auto slot = sensor_b_publisher.reserve().value();
    auto publishable = pinion::Publishable<InputMsg>::try_make(jewels::memory::make_non_null_from_ref(slot)).value();
    publishable.message() = InputMsg{};
    REQUIRE(slot.commit(k_publish_time));

    run_one_execution(jewels::time::SyncTime{std::chrono::milliseconds{1000 + (idx * 50)}});
  }

  auto available = event_group_subscriber.available();
  REQUIRE_FALSE(available.empty());

  EventGroupMsg msg{};
  deserialize_message(msg, available.begin()->message());

  for (const auto& entry : msg.get_signals())
  {
    const auto& meta = entry.get_sensor_b_unseen_messages_value_metadata();
    auto seqnos = meta.get_message_sequence_numbers();
    CHECK(seqnos.size() == 1);
  }
}

} // namespace
} // namespace clockwork::testing::cogs
