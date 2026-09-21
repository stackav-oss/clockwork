// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/cog_statistics.hh"
#include "clockwork/cog/include_common.hh"
#include "clockwork/dial/cond_messages_present.hh"
#include "clockwork/dial/cond_time_since_last_exec.hh"
#include "clockwork/dsl/cog/common_cog_event_metrics_clk_cc.hh"
#include "clockwork/dsl/cog/common_cog_telemetry_metrics_clk_cc.hh"
#include "clockwork/dsl/cog/ten_nanosecond_type.hh"
#include "clockwork/dsl/tests/support/hellocog.hh"
#include "clockwork/pinion/abstract_channel.hh"
#include "clockwork/pinion/in_memory_channel.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <memory_resource>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

namespace clockwork::testing::cogs
{

struct HelloCogMetricsTestFixture
{
  HelloCogMetricsTestFixture()
    : mem_res(std::pmr::new_delete_resource()), telemetry_metrics_tap(), event_metrics_tap()
  {
  }

  jewels::memory::MemoryResource mem_res;
  Tap<Tachyon<HelloCogTelemetryMetrics>> telemetry_metrics_tap;
  Tap<Tachyon<HelloCogEventMetricsBatch>> event_metrics_tap;
};

TEST_CASE_METHOD(HelloCogMetricsTestFixture, "HelloCog get_conditions_mask", "[hellocog][metrics]")
{
  using TimersTuple = typename HelloCogPolicy::TimersType::ConditionsTuple;
  using ConditionsTuple = typename HelloCogPolicy::ConditionsType::ConditionsTuple;
  using PeriodicCondition = TimeSinceLastExecCondition<500000000>;
  using AnyMsgCondition = MessagePresentCondition<1, 4294967295>;
  using NewMsgCondition = MessagePresentCondition<1, 2>;

  // No conditions active
  {
    const TimersTuple timers{PeriodicCondition{false, std::chrono::nanoseconds{0}}};
    const ConditionsTuple conditions{
      AnyMsgCondition{false, 0}, NewMsgCondition{false, 0}, AnyMsgCondition{false, 0}, AnyMsgCondition{false, 0}};
    const uint8_t trigger_mask = HelloCogPolicy::get_conditions_mask(timers, conditions);
    CHECK(trigger_mask == 0);
  }

  // Periodic timer active
  {
    const TimersTuple timers{PeriodicCondition{true, std::chrono::milliseconds{500}}};
    const ConditionsTuple conditions{
      AnyMsgCondition{false, 0}, NewMsgCondition{false, 0}, AnyMsgCondition{false, 0}, AnyMsgCondition{false, 0}};
    const uint8_t trigger_mask = HelloCogPolicy::get_conditions_mask(timers, conditions);
    CHECK(trigger_mask == static_cast<uint8_t>(HelloCogConditionsMask::periodic));
  }

  // any_msg active
  {
    const TimersTuple timers{PeriodicCondition{false, std::chrono::nanoseconds{0}}};
    const ConditionsTuple conditions{
      AnyMsgCondition{true, 1}, NewMsgCondition{false, 0}, AnyMsgCondition{false, 0}, AnyMsgCondition{false, 0}};
    const uint8_t trigger_mask = HelloCogPolicy::get_conditions_mask(timers, conditions);
    CHECK(trigger_mask == static_cast<uint8_t>(HelloCogConditionsMask::any_msg));
  }

  // new_msg active
  {
    const TimersTuple timers{PeriodicCondition{false, std::chrono::nanoseconds{0}}};
    const ConditionsTuple conditions{
      AnyMsgCondition{false, 0}, NewMsgCondition{true, 1}, AnyMsgCondition{false, 0}, AnyMsgCondition{false, 0}};
    const uint8_t trigger_mask = HelloCogPolicy::get_conditions_mask(timers, conditions);
    CHECK(trigger_mask == static_cast<uint8_t>(HelloCogConditionsMask::new_msg));
  }

  // new_multi_connect_hello__0 active
  {
    const TimersTuple timers{PeriodicCondition{false, std::chrono::nanoseconds{0}}};
    const ConditionsTuple conditions{
      AnyMsgCondition{false, 0}, NewMsgCondition{false, 0}, AnyMsgCondition{true, 1}, AnyMsgCondition{false, 0}};
    const uint8_t trigger_mask = HelloCogPolicy::get_conditions_mask(timers, conditions);
    CHECK(trigger_mask == static_cast<uint8_t>(HelloCogConditionsMask::new_multi_connect_hello__0));
  }

  // new_multi_connect_hello__1 active
  {
    const TimersTuple timers{PeriodicCondition{false, std::chrono::nanoseconds{0}}};
    const ConditionsTuple conditions{
      AnyMsgCondition{false, 0}, NewMsgCondition{false, 0}, AnyMsgCondition{false, 0}, AnyMsgCondition{true, 1}};
    const uint8_t trigger_mask = HelloCogPolicy::get_conditions_mask(timers, conditions);
    CHECK(trigger_mask == static_cast<uint8_t>(HelloCogConditionsMask::new_multi_connect_hello__1));
  }

  // All active
  {
    const TimersTuple timers{PeriodicCondition{true, std::chrono::milliseconds{500}}};
    const ConditionsTuple conditions{
      AnyMsgCondition{true, 1}, NewMsgCondition{true, 1}, AnyMsgCondition{true, 1}, AnyMsgCondition{true, 1}};
    const uint8_t trigger_mask = HelloCogPolicy::get_conditions_mask(timers, conditions);
    // NOLINTNEXTLINE(hicpp-signed-bitwise) false positive
    const uint8_t expected_mask = static_cast<uint8_t>(HelloCogConditionsMask::periodic) |
                                  static_cast<uint8_t>(HelloCogConditionsMask::any_msg) |
                                  static_cast<uint8_t>(HelloCogConditionsMask::new_msg) |
                                  static_cast<uint8_t>(HelloCogConditionsMask::new_multi_connect_hello__0) |
                                  static_cast<uint8_t>(HelloCogConditionsMask::new_multi_connect_hello__1);
    CHECK(trigger_mask == expected_mask);
  }
}

TEST_CASE_METHOD(HelloCogMetricsTestFixture, "HelloCog populate_output_telemetry_metrics", "[hellocog][metrics]")
{
  TelemetryMetrics telemetry_metrics{};
  std::ignore = telemetry_metrics.output_metrics[0].update(5); // out_world
  std::ignore = telemetry_metrics.output_metrics[1].update(3); // out_goodbye
  std::ignore = telemetry_metrics.output_metrics[2].update(2); // out_multi1
  std::ignore = telemetry_metrics.output_metrics[3].update(1); // out_multi2

  HelloCogPolicy::populate_output_telemetry_metrics(telemetry_metrics, telemetry_metrics_tap);

  const auto& tachyon_metrics = telemetry_metrics_tap;
  CHECK(tachyon_metrics.get_out_world_num_messages().get_max() == 5);
  CHECK(tachyon_metrics.get_out_goodbye_num_messages().get_max() == 3);
  CHECK(tachyon_metrics.get_out_multi1_num_messages().get_max() == 2);
  CHECK(tachyon_metrics.get_out_multi2_num_messages().get_max() == 1);
}

TEST_CASE_METHOD(HelloCogMetricsTestFixture, "HelloCog populate_output_event_metrics", "[hellocog][metrics]")
{
  std::pmr::vector<EventMetrics> event_metrics_vec{mem_res};
  auto& event = event_metrics_vec.emplace_back();
  event.output_metrics[0] = 1; // out_world
  event.output_metrics[1] = 0; // out_goodbye
  event.output_metrics[2] = 1; // out_multi1
  event.output_metrics[3] = 0; // out_multi2

  HelloCogPolicy::populate_output_event_metrics(event_metrics_vec, event_metrics_tap);

  REQUIRE(event_metrics_tap.get_event_metrics().size() == 1);
  const auto& tachyon_event = event_metrics_tap.get_event_metrics()[0];
  CHECK(tachyon_event.get_out_world_num_messages() == 1);
  CHECK(tachyon_event.get_out_goodbye_num_messages() == 0);
  CHECK(tachyon_event.get_out_multi1_num_messages() == 1);
  CHECK(tachyon_event.get_out_multi2_num_messages() == 0);
}

TEST_CASE_METHOD(HelloCogMetricsTestFixture, "HelloCog populate_trigger_mask", "[hellocog][metrics]")
{
  std::pmr::vector<EventMetrics> event_metrics_vec{};
  auto& event = event_metrics_vec.emplace_back();
  event.conditions_mask =
    static_cast<uint8_t>(HelloCogConditionsMask::periodic) | static_cast<uint8_t>(HelloCogConditionsMask::new_msg);

  HelloCogPolicy::populate_trigger_mask(event_metrics_vec, event_metrics_tap);

  REQUIRE(event_metrics_tap.get_event_metrics().size() == 1);
  const auto& tachyon_event = event_metrics_tap.get_event_metrics()[0];
  CHECK(static_cast<uint8_t>(tachyon_event.get_trigger_flags()) == event.conditions_mask);
}

template <typename PolicyType>
struct InputViewFixture // NOLINT(clang-analyzer-optin.performance.Padding) Test code so performance is not a concern.
{
  using Policy = PolicyType;
  using MsgType = typename Policy::MsgType;
  using PinionDifferenceType = typename InputView<Policy>::PinionDifferenceType;
  static constexpr auto new_msgs_max_limit = std::numeric_limits<PinionDifferenceType>::max();

  InputViewFixture()
    : resource(std::pmr::new_delete_resource()),
      channel(std::make_shared<InMemoryChannel<MsgType, 5, false>>(resource)),
      publisher_handle(channel->make_publisher(1)),
      subscriber(channel, 10, resource, false)
  {
  }

  jewels::memory::MemoryResource resource;
  std::shared_ptr<InMemoryChannel<MsgType, 5, false>> channel;
  pinion::PublisherHandle publisher_handle;
  InputView<Policy> subscriber;
};

template <typename PolicyType>
class MockInputView : public InputView<PolicyType>
{
public:
  MockInputView(std::shared_ptr<pinion::AbstractChannel> subscriber, jewels::memory::MemoryResource& resource) noexcept
    : InputView<PolicyType>(std::move(subscriber), 10, resource, false)
  {
  }

  void set_input_metrics(const AggregatedInputMetrics& metrics)
  {
    mocked_input_metrics_ = metrics;
  }

  [[nodiscard]] const AggregatedInputMetrics& get_aggregated_input_metrics() const override
  {
    return mocked_input_metrics_;
  }

  AggregatedInputMetrics mocked_input_metrics_;

  ~MockInputView() override = default;
  MockInputView(const MockInputView&) = delete;
  MockInputView& operator=(const MockInputView&) = delete;
  MockInputView(MockInputView&&) = delete;
  MockInputView& operator=(MockInputView&&) = delete;
};

TEST_CASE_METHOD(HelloCogMetricsTestFixture, "HelloCog populate_input_event_metrics", "[hellocog][metrics]")
{
  const InputViewFixture<HelloCogPolicy::LatestHelloPolicy> fixture1;
  const InputViewFixture<HelloCogPolicy::MultiConnectHello_0Policy> fixture2;
  const InputViewFixture<HelloCogPolicy::MultiConnectHello_1Policy> fixture3;
  const InputViewFixture<HelloCogPolicy::HistoryOfHellosPolicy> fixture4;

  // Create mock input views with predefined metrics
  auto mock_input1 = std::make_shared<MockInputView<HelloCogPolicy::LatestHelloPolicy>>(fixture1.channel, mem_res);
  auto mock_input2 =
    std::make_shared<MockInputView<HelloCogPolicy::MultiConnectHello_0Policy>>(fixture2.channel, mem_res);
  auto mock_input3 =
    std::make_shared<MockInputView<HelloCogPolicy::MultiConnectHello_1Policy>>(fixture3.channel, mem_res);
  auto mock_input4 = std::make_shared<MockInputView<HelloCogPolicy::HistoryOfHellosPolicy>>(fixture4.channel, mem_res);
  // Set up input metrics for mock_input1
  AggregatedInputMetrics input1_metrics{};
  input1_metrics.event_metrics.emplace_back(
    InputEventMetrics{.num_unseen_messages = 5, .message_staleness = TenNanoseconds{100}, .messages_dropped = 2});
  input1_metrics.event_metrics.emplace_back(
    InputEventMetrics{.num_unseen_messages = 3, .message_staleness = TenNanoseconds{200}, .messages_dropped = 1});
  mock_input1->set_input_metrics(input1_metrics);

  // Set up input metrics for mock_input2
  AggregatedInputMetrics input2_metrics{};
  input2_metrics.event_metrics.emplace_back(
    InputEventMetrics{.num_unseen_messages = 6, .message_staleness = TenNanoseconds{101}, .messages_dropped = 3});
  input2_metrics.event_metrics.emplace_back(
    InputEventMetrics{.num_unseen_messages = 4, .message_staleness = TenNanoseconds{201}, .messages_dropped = 2});
  mock_input2->set_input_metrics(input2_metrics);

  // Set up input metrics for mock_input3
  AggregatedInputMetrics input3_metrics{};
  input3_metrics.event_metrics.emplace_back(
    InputEventMetrics{.num_unseen_messages = 7, .message_staleness = TenNanoseconds{102}, .messages_dropped = 4});
  input3_metrics.event_metrics.emplace_back(
    InputEventMetrics{.num_unseen_messages = 5, .message_staleness = TenNanoseconds{203}, .messages_dropped = 3});
  mock_input3->set_input_metrics(input3_metrics);

  // Set up input metrics for mock_input4
  AggregatedInputMetrics input4_metrics{};
  input4_metrics.event_metrics.emplace_back(
    InputEventMetrics{.num_unseen_messages = 2, .message_staleness = TenNanoseconds{150}, .messages_dropped = 0});
  input4_metrics.event_metrics.emplace_back(
    InputEventMetrics{.num_unseen_messages = 4, .message_staleness = TenNanoseconds{75}, .messages_dropped = 3});
  mock_input4->set_input_metrics(input4_metrics);

  // Create subscribers tuple
  auto subscribers = std::make_tuple(mock_input1, mock_input2, mock_input3, mock_input4);

  HelloCogPolicy::populate_input_event_metrics(subscribers, event_metrics_tap);

  // Verify that event metrics were populated correctly
  REQUIRE(event_metrics_tap.get_event_metrics().size() == 2);

  const auto& tachyon_event_0 = event_metrics_tap.get_event_metrics()[0];
  CHECK(tachyon_event_0.get_latest_hello().get_unseen_messages() == 5);
  CHECK(tachyon_event_0.get_latest_hello().get_staleness() == ten_nanoseconds_factory(100));
  CHECK(tachyon_event_0.get_latest_hello().get_dropped_messages() == 2);
  CHECK(tachyon_event_0.get_multi_connect_hello__0().get_unseen_messages() == 6);
  CHECK(tachyon_event_0.get_multi_connect_hello__0().get_staleness() == ten_nanoseconds_factory(101));
  CHECK(tachyon_event_0.get_multi_connect_hello__0().get_dropped_messages() == 3);
  CHECK(tachyon_event_0.get_multi_connect_hello__1().get_unseen_messages() == 7);
  CHECK(tachyon_event_0.get_multi_connect_hello__1().get_staleness() == ten_nanoseconds_factory(102));
  CHECK(tachyon_event_0.get_multi_connect_hello__1().get_dropped_messages() == 4);
  CHECK(tachyon_event_0.get_history_of_hellos().get_unseen_messages() == 2);
  CHECK(tachyon_event_0.get_history_of_hellos().get_staleness() == ten_nanoseconds_factory(150));
  CHECK(tachyon_event_0.get_history_of_hellos().get_dropped_messages() == 0);

  const auto& tachyon_event_1 = event_metrics_tap.get_event_metrics()[1];
  CHECK(tachyon_event_1.get_latest_hello().get_unseen_messages() == 3);
  CHECK(tachyon_event_1.get_latest_hello().get_staleness() == ten_nanoseconds_factory(200));
  CHECK(tachyon_event_1.get_latest_hello().get_dropped_messages() == 1);
  CHECK(tachyon_event_1.get_multi_connect_hello__0().get_unseen_messages() == 4);
  CHECK(tachyon_event_1.get_multi_connect_hello__0().get_staleness() == ten_nanoseconds_factory(201));
  CHECK(tachyon_event_1.get_multi_connect_hello__0().get_dropped_messages() == 2);
  CHECK(tachyon_event_1.get_multi_connect_hello__1().get_unseen_messages() == 5);
  CHECK(tachyon_event_1.get_multi_connect_hello__1().get_staleness() == ten_nanoseconds_factory(203));
  CHECK(tachyon_event_1.get_multi_connect_hello__1().get_dropped_messages() == 3);
  CHECK(tachyon_event_1.get_history_of_hellos().get_unseen_messages() == 4);
  CHECK(tachyon_event_1.get_history_of_hellos().get_staleness() == ten_nanoseconds_factory(75));
  CHECK(tachyon_event_1.get_history_of_hellos().get_dropped_messages() == 3);
}

TEST_CASE_METHOD(HelloCogMetricsTestFixture, "HelloCog populate_condition_trigger_vals", "[hellocog][metrics]")
{
  std::pmr::vector<uint64_t> condition_trigger_counts{mem_res};

  // Test with multiple condition trigger masks
  condition_trigger_counts.push_back(static_cast<uint64_t>(HelloCogConditionsMask::periodic));
  condition_trigger_counts.push_back(static_cast<uint64_t>(HelloCogConditionsMask::any_msg));
  condition_trigger_counts.push_back(static_cast<uint64_t>(HelloCogConditionsMask::new_msg));
  condition_trigger_counts.push_back(
    static_cast<uint64_t>(HelloCogConditionsMask::periodic) | static_cast<uint64_t>(HelloCogConditionsMask::any_msg));
  condition_trigger_counts.push_back(
    static_cast<uint64_t>(HelloCogConditionsMask::periodic) | static_cast<uint64_t>(HelloCogConditionsMask::new_msg));

  HelloCogPolicy::populate_condition_trigger_vals(condition_trigger_counts, telemetry_metrics_tap);

  // Verify the condition trigger counts
  // periodic appears in 3 masks: periodic only, periodic+any_msg, periodic+new_msg
  CHECK(telemetry_metrics_tap.get_periodic_trigger_vals() == 3);
  // any_msg appears in 2 masks: any_msg only, periodic+any_msg
  CHECK(telemetry_metrics_tap.get_any_msg_trigger_vals() == 2);
  // new_msg appears in 2 masks: new_msg only, periodic+new_msg
  CHECK(telemetry_metrics_tap.get_new_msg_trigger_vals() == 2);
}

TEST_CASE_METHOD(HelloCogMetricsTestFixture, "HelloCog populate_telemetry_triggers", "[hellocog][metrics]")
{
  TelemetryMetrics telemetry_metrics{
    .output_metrics{mem_res}, .conditions_mask_vector{mem_res}, .publisher_throttle_counts{mem_res}};

  // Set up conditions mask vector with various trigger combinations to create different counts for each condition
  // periodic: appears in 5 masks
  telemetry_metrics.conditions_mask_vector.push_back(static_cast<uint64_t>(HelloCogConditionsMask::periodic));
  telemetry_metrics.conditions_mask_vector.push_back(static_cast<uint64_t>(HelloCogConditionsMask::periodic));
  telemetry_metrics.conditions_mask_vector.push_back(
    static_cast<uint64_t>(HelloCogConditionsMask::periodic) | static_cast<uint64_t>(HelloCogConditionsMask::any_msg));
  telemetry_metrics.conditions_mask_vector.push_back(
    static_cast<uint64_t>(HelloCogConditionsMask::periodic) | static_cast<uint64_t>(HelloCogConditionsMask::new_msg));
  telemetry_metrics.conditions_mask_vector.push_back(
    static_cast<uint64_t>(HelloCogConditionsMask::periodic) | static_cast<uint64_t>(HelloCogConditionsMask::any_msg) |
    static_cast<uint64_t>(HelloCogConditionsMask::new_msg));

  // any_msg: appears in 3 masks
  telemetry_metrics.conditions_mask_vector.push_back(static_cast<uint64_t>(HelloCogConditionsMask::any_msg));
  telemetry_metrics.conditions_mask_vector.push_back(
    static_cast<uint64_t>(HelloCogConditionsMask::any_msg) | static_cast<uint64_t>(HelloCogConditionsMask::new_msg));

  // new_msg: appears in 2 additional masks (already counted 2 above with periodic)
  telemetry_metrics.conditions_mask_vector.push_back(static_cast<uint64_t>(HelloCogConditionsMask::new_msg));

  HelloCogPolicy::populate_telemetry_triggers(telemetry_metrics, telemetry_metrics_tap);

  // Verify the condition trigger counts from conditions_mask_vector
  // periodic appears in 5 masks total
  CHECK(telemetry_metrics_tap.get_periodic_trigger_vals() == 5);
  // any_msg appears in 3 masks total
  CHECK(telemetry_metrics_tap.get_any_msg_trigger_vals() == 4);
  // new_msg appears in 4 masks total (2 with periodic, 1 with any_msg, 1 standalone)
  CHECK(telemetry_metrics_tap.get_new_msg_trigger_vals() == 4);
}

TEST_CASE_METHOD(HelloCogMetricsTestFixture, "HelloCog populate_input_telemetry_metrics", "[hellocog][metrics]")
{
  const InputViewFixture<HelloCogPolicy::LatestHelloPolicy> fixture1;
  const InputViewFixture<HelloCogPolicy::MultiConnectHello_0Policy> fixture2;
  const InputViewFixture<HelloCogPolicy::MultiConnectHello_1Policy> fixture3;
  const InputViewFixture<HelloCogPolicy::HistoryOfHellosPolicy> fixture4;

  auto mock_input1 = std::make_shared<MockInputView<HelloCogPolicy::LatestHelloPolicy>>(fixture1.channel, mem_res);
  auto mock_input2 =
    std::make_shared<MockInputView<HelloCogPolicy::MultiConnectHello_0Policy>>(fixture2.channel, mem_res);
  auto mock_input3 =
    std::make_shared<MockInputView<HelloCogPolicy::MultiConnectHello_1Policy>>(fixture3.channel, mem_res);
  auto mock_input4 = std::make_shared<MockInputView<HelloCogPolicy::HistoryOfHellosPolicy>>(fixture4.channel, mem_res);

  // Set up telemetry metrics for mock_input1
  AggregatedInputMetrics input1_metrics{};
  std::ignore = input1_metrics.telemetry_metrics.message_staleness.update(ten_nanoseconds_factory(100));
  std::ignore = input1_metrics.telemetry_metrics.message_staleness.update(ten_nanoseconds_factory(200));
  std::ignore = input1_metrics.telemetry_metrics.message_staleness.update(ten_nanoseconds_factory(150));
  std::ignore = input1_metrics.telemetry_metrics.num_unseen_messages.update(5);
  std::ignore = input1_metrics.telemetry_metrics.num_unseen_messages.update(3);
  std::ignore = input1_metrics.telemetry_metrics.num_unseen_messages.update(7);
  std::ignore = input1_metrics.telemetry_metrics.messages_dropped.update(2);
  std::ignore = input1_metrics.telemetry_metrics.messages_dropped.update(1);
  mock_input1->set_input_metrics(input1_metrics);

  // Set up telemetry metrics for mock_input2
  AggregatedInputMetrics input2_metrics{};
  std::ignore = input2_metrics.telemetry_metrics.message_staleness.update(ten_nanoseconds_factory(81));
  std::ignore = input2_metrics.telemetry_metrics.message_staleness.update(ten_nanoseconds_factory(121));
  std::ignore = input2_metrics.telemetry_metrics.num_unseen_messages.update(5);
  std::ignore = input2_metrics.telemetry_metrics.num_unseen_messages.update(7);
  std::ignore = input2_metrics.telemetry_metrics.messages_dropped.update(1);
  std::ignore = input2_metrics.telemetry_metrics.messages_dropped.update(4);
  mock_input2->set_input_metrics(input2_metrics);

  // Set up telemetry metrics for mock_input3
  AggregatedInputMetrics input3_metrics{};
  std::ignore = input3_metrics.telemetry_metrics.message_staleness.update(ten_nanoseconds_factory(82));
  std::ignore = input3_metrics.telemetry_metrics.message_staleness.update(ten_nanoseconds_factory(122));
  std::ignore = input3_metrics.telemetry_metrics.num_unseen_messages.update(6);
  std::ignore = input3_metrics.telemetry_metrics.num_unseen_messages.update(8);
  std::ignore = input3_metrics.telemetry_metrics.messages_dropped.update(2);
  std::ignore = input3_metrics.telemetry_metrics.messages_dropped.update(5);
  mock_input3->set_input_metrics(input3_metrics);

  // Set up telemetry metrics for mock_input4
  AggregatedInputMetrics input4_metrics{};
  std::ignore = input4_metrics.telemetry_metrics.message_staleness.update(ten_nanoseconds_factory(80));
  std::ignore = input4_metrics.telemetry_metrics.message_staleness.update(ten_nanoseconds_factory(120));
  std::ignore = input4_metrics.telemetry_metrics.num_unseen_messages.update(4);
  std::ignore = input4_metrics.telemetry_metrics.num_unseen_messages.update(6);
  std::ignore = input4_metrics.telemetry_metrics.messages_dropped.update(0);
  std::ignore = input4_metrics.telemetry_metrics.messages_dropped.update(3);
  mock_input4->set_input_metrics(input4_metrics);

  // Create subscribers tuple
  auto subscribers = std::make_tuple(mock_input1, mock_input2, mock_input3, mock_input4);

  HelloCogPolicy::populate_input_telemetry_metrics(subscribers, telemetry_metrics_tap);

  // Verify telemetry metrics were populated correctly
  // Check that the metrics contain the aggregated values from the input views
  // For latest_hello input (mock_input1)
  CHECK(telemetry_metrics_tap.get_latest_hello().get_staleness().get_min() == ten_nanoseconds_factory(100));
  CHECK(telemetry_metrics_tap.get_latest_hello().get_staleness().get_max() == ten_nanoseconds_factory(200));
  CHECK(telemetry_metrics_tap.get_latest_hello().get_unseen_messages().get_min() == 3);
  CHECK(telemetry_metrics_tap.get_latest_hello().get_unseen_messages().get_max() == 7);
  CHECK(telemetry_metrics_tap.get_latest_hello().get_dropped_messages().get_min() == 1);
  CHECK(telemetry_metrics_tap.get_latest_hello().get_dropped_messages().get_max() == 2);

  // For multi_connect_hello__0 input (mock_input2)
  CHECK(telemetry_metrics_tap.get_multi_connect_hello__0().get_staleness().get_min() == ten_nanoseconds_factory(81));
  CHECK(telemetry_metrics_tap.get_multi_connect_hello__0().get_staleness().get_max() == ten_nanoseconds_factory(121));
  CHECK(telemetry_metrics_tap.get_multi_connect_hello__0().get_unseen_messages().get_min() == 5);
  CHECK(telemetry_metrics_tap.get_multi_connect_hello__0().get_unseen_messages().get_max() == 7);
  CHECK(telemetry_metrics_tap.get_multi_connect_hello__0().get_dropped_messages().get_min() == 1);
  CHECK(telemetry_metrics_tap.get_multi_connect_hello__0().get_dropped_messages().get_max() == 4);

  // For multi_connect_hello__1 input (mock_input3)
  CHECK(telemetry_metrics_tap.get_multi_connect_hello__1().get_staleness().get_min() == ten_nanoseconds_factory(82));
  CHECK(telemetry_metrics_tap.get_multi_connect_hello__1().get_staleness().get_max() == ten_nanoseconds_factory(122));
  CHECK(telemetry_metrics_tap.get_multi_connect_hello__1().get_unseen_messages().get_min() == 6);
  CHECK(telemetry_metrics_tap.get_multi_connect_hello__1().get_unseen_messages().get_max() == 8);
  CHECK(telemetry_metrics_tap.get_multi_connect_hello__1().get_dropped_messages().get_min() == 2);
  CHECK(telemetry_metrics_tap.get_multi_connect_hello__1().get_dropped_messages().get_max() == 5);

  // For history_of_hellos input (mock_input4)
  CHECK(telemetry_metrics_tap.get_history_of_hellos().get_staleness().get_min() == ten_nanoseconds_factory(80));
  CHECK(telemetry_metrics_tap.get_history_of_hellos().get_staleness().get_max() == ten_nanoseconds_factory(120));
  CHECK(telemetry_metrics_tap.get_history_of_hellos().get_unseen_messages().get_min() == 4);
  CHECK(telemetry_metrics_tap.get_history_of_hellos().get_unseen_messages().get_max() == 6);
  CHECK(telemetry_metrics_tap.get_history_of_hellos().get_dropped_messages().get_min() == 0);
  CHECK(telemetry_metrics_tap.get_history_of_hellos().get_dropped_messages().get_max() == 3);
}
} // namespace clockwork::testing::cogs
