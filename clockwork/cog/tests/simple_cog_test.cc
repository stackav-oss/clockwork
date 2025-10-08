// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/cog_conditions.hh"
#include "clockwork/cog/cog_configs.hh"
#include "clockwork/cog/cog_diagnostics.hh"
#include "clockwork/cog/cog_infra_diagnostics.hh"
#include "clockwork/cog/cog_inputs.hh"
#include "clockwork/cog/cog_memory_resources.hh"
#include "clockwork/cog/cog_publishers.hh"
#include "clockwork/cog/cog_state.hh"
#include "clockwork/cog/cog_states.hh"
#include "clockwork/cog/cog_statistics.hh"
#include "clockwork/cog/cog_timers.hh"
#include "clockwork/cog/detail.hh"
#include "clockwork/cog/input_condition.hh"
#include "clockwork/cog/simple_cog.hh"
#include "clockwork/cog/tests/support/fake_cog.hh"
#include "clockwork/cog/tests/support/test_cog_event_metrics_cpp.hh"
#include "clockwork/cog/tests/support/test_cog_telemetry_metrics_cpp.hh"
#include "clockwork/common/abstract_cog.hh"
#include "clockwork/common/abstract_cog_queue.hh"
#include "clockwork/common/abstract_timer.hh"
#include "clockwork/common/cog_envelope.hh"
#include "clockwork/common/cog_execution_error.hh"
#include "clockwork/common/process_description.hh"
#include "clockwork/diagnostics/report.hh"
#include "clockwork/diagnostics/report_definitions.hh"
#include "clockwork/diagnostics/reporter.hh"
#include "clockwork/dial/cond_messages_present.hh"
#include "clockwork/dial/cond_time_since_last_exec.hh"
#include "clockwork/dial/msg_input.hh"
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/in_memory_channel.hh"
#include "clockwork/pinion/observer.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "jewels/container/compare.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_shared_ptr.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <boost/iterator/iterator_facade.hpp>
#include <catch2/catch_test_macros.hpp>
#include <gsl/util>

#include <array>
#include <chrono>
#include <condition_variable>
#include <csetjmp>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <iterator>
#include <limits>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <optional>
#include <ranges>
#include <ratio>
#include <stdexcept>
#include <string_view>
#include <sys/types.h>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace clockwork
{
namespace
{

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) For testing purposes only.
jmp_buf terminate_handler_jmp;

void terminate_handler()
{
  // NOLINTNEXTLINE(cert-err52-cpp, cppcoreguidelines-pro-bounds-array-to-pointer-decay)  For testing purposes only
  longjmp(terminate_handler_jmp, 1);
}

/// Fixed timestamp for publish timestamp.
inline constexpr auto fake_publish_time{jewels::time::SyncTime{std::chrono::nanoseconds{12345}}};

struct TestConfig
{
  uint32_t value = {};
};

struct TestState
{
  explicit TestState(jewels::memory::MemoryResource /*unused*/) {}

  uint32_t value = {};
};

struct TestInput
{
  uint32_t value = {};
};

struct TestOutput
{
  uint32_t value = {};
};

class TestTimer : public AbstractTimer
{
public:
  [[nodiscard]] int32_t descriptor() const override
  {
    return 0;
  }

  void set_observer(pinion::Observer* /*observer*/) override {}
  [[nodiscard]] bool start(jewels::time::SyncTime trigger_at, std::chrono::nanoseconds /* period */) override
  {
    trigger_at_ = trigger_at;
    ++start_count;
    return true;
  }

  [[nodiscard]] bool stop() override
  {
    trigger_at_ = {};
    ++stop_count;
    return true;
  }

  void notify(AbstractEPollManager& /*epoll*/, int /*efd*/, uint32_t /*events*/) override {}

  size_t start_count = {};
  size_t stop_count = {};
  std::optional<jewels::time::SyncTime> trigger_at_ = {};
};

class TestCogQueue : public AbstractCogQueue
{
public:
  TestCogQueue() = default;
  ~TestCogQueue() override = default;
  TestCogQueue(const TestCogQueue&) = delete;
  TestCogQueue& operator=(const TestCogQueue&) = delete;
  TestCogQueue(TestCogQueue&&) = delete;
  TestCogQueue& operator=(TestCogQueue&&) = delete;

  void notify() override
  {
    ++notify_count;
  }

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

  size_t notify_count = {};
  std::deque<CogEnvelope> queue = {};
};

struct TestCogDial // NOLINT(cppcoreguidelines-pro-type-member-init). Initialization is uneeded for the relevant test
                   // cases
{
  /// The time the Cog user function was called.
  jewels::time::SyncTime start_time;

  /// Resources
  struct MemoryResources
  {
    jewels::memory::MemoryResource test_memres;
  };
  MemoryResources resources;

  /// Configs
  struct Configs
  {
    const TestConfig& test_configs; // Needs to be a reference for the purpose of this test.
  };
  Configs configs;

  /// States
  struct States
  {
    jewels::memory::ObjectPtr<TestState> test_state;
  };
  States states;

  /// Inputs
  struct Inputs
  {
    MessageInputDial<TestInput, 3> test_inputs;
    MessageInputDial<TestInput, 3> no_cond_inputs;
  };
  Inputs inputs;

  /// Outputs
  struct Outputs
  {
    pinion::Publishable<TestOutput> test_output;
    pinion::Publishable<Tap<::clockwork::Tachyon<::clockwork::cog::metrics::test::TestCogTelemetryMetrics>>>
      telemetry_metrics;
    pinion::Publishable<Tap<::clockwork::Tachyon<::clockwork::cog::metrics::test::TestCogEventMetricsBatch>>>
      event_metrics;
  };
  Outputs outputs;

  /// Conditions
  struct Conditions
  {
    TimeSinceLastExecCondition<1'000'000U> periodic;
    MessagePresentCondition<1U, std::numeric_limits<uint32_t>::max()> new_msg;
  };
  Conditions conditions;

  /// Diagnostics
  diagnostics::ClockworkManager<diagnostics::SignalGroupId::fault_injector_a>::Reporter* diagnostics;
};

struct TestCogPolicy
{

  static constexpr auto cog_id =
    jewels::Uuid<common::CogClassId>::from_string("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee").value();
  static constexpr auto name = "clockwork::TestCogPolicy";
  static constexpr size_t event_metrics_batch_size = 10U;
  static constexpr auto simulated_execution_duration = std::chrono::milliseconds(0);
  static constexpr auto publish_metrics = true;
  static constexpr auto telemetry_metrics_index = 1;
  static constexpr auto event_metrics_index = 2;
  /// MemoryResources

  struct TestMemoryResourcePolicy
  {
    using MemoryResourceType = jewels::memory::MemoryResource;
    static constexpr auto endpoint_id =
      jewels::Uuid<common::EndpointClassId>::from_string("d68c4441-7c8f-4377-bccf-260e363cec15").value();
    static constexpr std::string_view name = "TestMemoryResourcePolicy";
  };

  using MemoryResourcesType = CogMemoryResources<TestMemoryResourcePolicy>;

  /// Configs

  struct TestConfigPolicy
  {
    using ConfigType = TestConfig;
    static constexpr auto endpoint_id =
      jewels::Uuid<common::EndpointClassId>::from_string("4860552f-18f4-495f-901d-a08a7ee344d1").value();
    static constexpr std::string_view name = "TestConfigPolicy";
  };

  using ConfigsType = CogConfigs<TestConfigPolicy>;

  /// States

  struct TestStatePolicy
  {
    using StateType = TestState;
    static constexpr auto endpoint_id =
      jewels::Uuid<common::EndpointClassId>::from_string("bba0e123-5176-441e-a639-b28ad5106149").value();
    static constexpr std::string_view name = "TestStatePolicy";
    static constexpr bool read_only = false;
  };

  using StatesType = CogStates<TestStatePolicy>;

  /// Timers

  struct TimeSinceLastExecPolicy
  {
    static constexpr int64_t threshold_ns = 1'000'000; // 1 ms
    static constexpr auto endpoint_id =
      jewels::Uuid<common::EndpointClassId>::from_string("dc4299c6-37eb-4448-82b4-6b196cf0c519").value();
    static constexpr std::string_view name = "TimeSinceLastExecPolicy";
  };

  using TimersType = CogTimers<TimeSinceLastExecPolicy>;

  /// Inputs

  struct TestInputPolicy
  {
    using MsgType = TestInput;
    static constexpr auto endpoint_id =
      jewels::Uuid<common::EndpointClassId>::from_string("01234567-89ab-cdef-fedc-ba9876543210").value();
    static constexpr std::string_view name = "TestInputPolicy";
    static constexpr auto max_view_size = 3U;
    static constexpr std::optional<::ssize_t> safety_margin{};
    static constexpr std::optional<size_t> skip_threshold{};
    static constexpr auto copy_inputs = false;
    static constexpr auto manual_cursor = false;
    // Testing only
    static constexpr auto channel_size = 3U;
  };

  struct NoCondInputPolicy
  {
    using MsgType = TestInput;
    static constexpr auto endpoint_id =
      jewels::Uuid<common::EndpointClassId>::from_string("358094f0-d7bc-4b2f-bee8-1144083fdbde").value();
    static constexpr std::string_view name = "NoCondInputPolicy";
    static constexpr auto max_view_size = 3U;
    static constexpr std::optional<::ssize_t> safety_margin{};
    static constexpr std::optional<size_t> skip_threshold{};
    static constexpr auto copy_inputs = false;
    static constexpr auto manual_cursor = false;
    // Testing only
    static constexpr auto channel_size = 3U;
  };

  using InputsType = CogInputs<TestInputPolicy, NoCondInputPolicy>;

  /// Conditions

  struct TestConditionPolicy
  {
    using MsgType = TestInput;
    static constexpr auto endpoint_id =
      jewels::Uuid<common::EndpointClassId>::from_string("01234567-89ab-cdef-fedc-ba9876543210").value();
    static constexpr std::string_view name = "TestConditionPolicy";
    static constexpr auto bounds_min = 1U;
    static constexpr auto bounds_max = std::numeric_limits<uint32_t>::max();
    static constexpr auto condition_type = InputConditionType::new_message;
  };

  using ConditionsType = CogConditions<TestConditionPolicy>;

  static uint8_t get_conditions_mask(
    const typename TimersType::ConditionsTuple& /*timers*/,
    const typename ConditionsType::ConditionsTuple& /*conditions*/)
  {
    return 0;
  } /// Publishers

  struct TestOutputPolicy
  {
    using MsgType = TestOutput;
    [[maybe_unused]] static constexpr auto endpoint_id =
      jewels::Uuid<common::EndpointClassId>::from_string("23456701-ab89-efcd-dcfe-9876543210ba").value();
    static constexpr std::string_view name = "TestOutputPolicy";
    // Testing only
    static constexpr auto channel_size = 3U;
    static constexpr std::optional<clockwork::RateLimitParameters> rate_limit_params{};
  };

  struct CogTelemetryMetricsPolicy
  {
    using MsgType = ::clockwork::Tap<::clockwork::Tachyon<::clockwork::cog::metrics::test::TestCogTelemetryMetrics>>;
    static constexpr auto endpoint_id =
      ::jewels::Uuid<::clockwork::common::EndpointClassId>::from_string("314ede8b-3a03-571f-a1ea-c89deff62d58").value();
    static constexpr ::std::string_view name = "TestCogTelemetryMetricsPolicy";
    static constexpr auto channel_size = 3U;
    static constexpr std::optional<::clockwork::RateLimitParameters> rate_limit_params{};
  };
  struct CogEventMetricsPolicy
  {
    using MsgType = ::clockwork::Tap<::clockwork::Tachyon<::clockwork::cog::metrics::test::TestCogEventMetricsBatch>>;
    static constexpr auto endpoint_id =
      ::jewels::Uuid<::clockwork::common::EndpointClassId>::from_string("32f0add4-c499-53b5-9919-8f807168cc01").value();
    static constexpr ::std::string_view name = "TestCogEventMetricsPolicy";
    static constexpr auto channel_size = 3U;
    static constexpr std::optional<::clockwork::RateLimitParameters> rate_limit_params{};
  };
  using PublishersType = CogPublishers<TestOutputPolicy, CogTelemetryMetricsPolicy, CogEventMetricsPolicy>;
  static void populate_output_event_metrics(
    const std::pmr::vector<EventMetrics>& /*outputs*/,
    Tap<Tachyon<cog::metrics::test::TestCogEventMetricsBatch>>& /*event_metrics_tachyon*/)
  {
  }

  static void populate_trigger_mask(
    const std::pmr::vector<EventMetrics>& /*outputs*/,
    Tap<Tachyon<cog::metrics::test::TestCogEventMetricsBatch>>& /*event_metrics_tachyon*/)
  {
  }

  static void populate_telemetry_triggers(
    const TelemetryMetrics& /*outputs*/,
    Tap<Tachyon<cog::metrics::test::TestCogTelemetryMetrics>>& /*telemetry_metrics_tachyon*/)
  {
  }

  static void populate_output_telemetry_metrics(
    const TelemetryMetrics& /*outputs*/,
    Tap<Tachyon<cog::metrics::test::TestCogTelemetryMetrics>>& /*telemetry_metrics_tachyon*/)
  {
  }

  static void populate_input_event_metrics(
    const typename InputsType::SubscribersTuple& /*inputs*/,
    Tap<Tachyon<cog::metrics::test::TestCogEventMetricsBatch>>& /*event_metrics_tachyon*/)
  {
  }

  static void populate_input_telemetry_metrics(
    const typename InputsType::SubscribersTuple& /*inputs*/,
    Tap<Tachyon<cog::metrics::test::TestCogTelemetryMetrics>>& /*event_metrics_tachyon*/)
  {
  }

  static void populate_telemetry_metrics(
    const TelemetryMetrics& /*outputs*/,
    const typename InputsType::SubscribersTuple& /*inputs*/,
    typename PublishersType::PublishablesTuple& publishables)
  {
    std::get<telemetry_metrics_index>(publishables).mark_for_publish();
  }

  static void populate_event_metrics(
    const std::pmr::vector<EventMetrics>& /*outputs*/,
    const typename InputsType::SubscribersTuple& /*inputs*/,
    typename PublishersType::PublishablesTuple& publishables)
  {
    std::get<event_metrics_index>(publishables).mark_for_publish();
  }

  /// Diagnostics

  struct DiagnosticsPolicy
  {
    static constexpr auto endpoint_id =
      jewels::Uuid<common::EndpointClassId>::from_string("669db3b2-3c29-42b6-8c19-475d8cb7bc67").value();
    static constexpr std::string_view member_name = "diagnostics";
    static constexpr std::string_view group_name = "test_group";
    static constexpr std::string_view instance_name;
    using ManagerType = diagnostics::ClockworkManager<diagnostics::SignalGroupId::fault_injector_a>;
  };

  using DiagnosticsType = CogDiagnostics<DiagnosticsPolicy>;

  /// Infra Diagnostics

  using InfraDiagnosticsType = CogInfraDiagnostics<testing::FakeCogInfraDiagnostics<2, 1>::CogInfraDiagnosticsPolicy>;

  /// Check the cog conditions for readiness.
  /// @param[in] timers The set of timer conditions
  /// @param[in] conditions The set of subscriber conditions
  /// @return True if the execution conditions are met
  [[nodiscard]] static bool is_ready(
    CogStatistics& /*statistics*/,
    typename TimersType::ConditionsTuple& timers,
    typename ConditionsType::ConditionsTuple& conditions)
  {
    return (static_cast<bool>(std::get<0>(timers)) && static_cast<bool>(std::get<0>(conditions)));
  }

  /// Make the dial inputs for the cog
  // NOLINTNEXTLINE(readability-function-size) Needs to match the signature of the make_dial function
  [[nodiscard]] static TestCogDial make_dial(
    const CogExecuteParams& params,
    const typename MemoryResourcesType::MemoryResourcesTuple& resources,
    const typename ConfigsType::ConfigsTuple& configs,
    const typename StatesType::StatesTuple& states,
    typename InputsType::InputDialTuple inputs,
    typename PublishersType::PublishablesTuple publishables,
    typename TimersType::ConditionsTuple& timer_conditions,
    typename ConditionsType::ConditionsTuple& input_conditions,
    typename DiagnosticsType::ReporterType& diagnostics)
  {
    return TestCogDial{
      .start_time = params.start_time,
      .resources =
        {
          .test_memres = std::get<0>(resources),
        },
      .configs =
        {
          .test_configs = std::get<0>(configs),
        },
      .states =
        {
          .test_state = std::get<0>(states),
        },
      .inputs =
        {
          .test_inputs = std::get<0>(inputs),
          .no_cond_inputs = std::get<1>(inputs),
        },
      .outputs =
        {.test_output = std::move(std::get<0>(publishables)),
         .telemetry_metrics = std::move(std::get<1>(publishables)),
         .event_metrics = std::move(std::get<2>(publishables))},
      .conditions =
        {
          .periodic = std::get<0>(timer_conditions),
          .new_msg = std::get<0>(input_conditions),
        },
      .diagnostics = &diagnostics};
  }

  /// Execute the user defined function
  static void execute(TestCogDial dial)
  {
    REQUIRE(1 == std::distance(dial.inputs.test_inputs.get_cursor(), dial.inputs.test_inputs.end()));
    auto& output = dial.outputs.test_output.message();
    output = TestOutput{.value = dial.inputs.test_inputs.get_cursor()->value};
    dial.outputs.test_output.mark_for_publish();

    if (output.value == std::numeric_limits<uint32_t>::max())
    {
      throw std::logic_error{"The cog doesn't like this value"};
    }
  }
};

using TestCog = SimpleCog<TestCogPolicy>;

struct SimpleCogFixture // NOLINT(clang-analyzer-optin.performance.Padding) Test code so performance is not a concern.
{
  using MemoryResourcePolicy = typename TestCogPolicy::TestMemoryResourcePolicy;
  using ConfigPolicy = typename TestCogPolicy::TestConfigPolicy;
  using StatePolicy = typename TestCogPolicy::TestStatePolicy;
  using TimerPolicy = typename TestCogPolicy::TimeSinceLastExecPolicy;
  using InputPolicy = typename TestCogPolicy::TestInputPolicy;
  using NoCondInputPolicy = typename TestCogPolicy::NoCondInputPolicy;
  using OutputPolicy = typename TestCogPolicy::TestOutputPolicy;
  using DiagnosticsPolicy = typename TestCogPolicy::DiagnosticsPolicy;

  SimpleCogFixture()
    : resource(std::pmr::new_delete_resource()),
      instance_id(jewels::Uuid<common::CogInstanceId>::random_uuid()),
      cog(resource, instance_id, jewels::memory::make_non_null_from_ref(queue)),
      timer(std::make_shared<TestTimer>()),
      input_channel(resource),
      publisher(input_channel.make_publisher(1)),
      no_cond_input_channel(resource),
      no_cond_publisher(no_cond_input_channel.make_publisher(1)),
      output_channel(resource),
      telemetry_channel(resource),
      event_channel(resource),
      subscriber(output_channel.make_subscriber()),
      telemetry_subscriber(telemetry_channel.make_subscriber()),
      event_subscriber(event_channel.make_subscriber()),
      diagnostics_channel(resource)
  {
  }

  void publish(TestInput msg)
  {
    auto slot = publisher.reserve().value();
    auto publishable = pinion::Publishable<TestInput>::try_make(jewels::memory::make_non_null_from_ref(slot)).value();
    publishable.message() = msg;
    REQUIRE(slot.commit(fake_publish_time));
  }

  jewels::memory::MemoryResource resource;
  clockwork::TestCogQueue queue;
  jewels::Uuid<common::CogInstanceId> instance_id;
  TestCog cog;

  std::shared_ptr<TestTimer> timer;

  InMemoryChannel<typename InputPolicy::MsgType, InputPolicy::channel_size> input_channel;
  pinion::PublisherHandle publisher;

  InMemoryChannel<typename NoCondInputPolicy::MsgType, NoCondInputPolicy::channel_size> no_cond_input_channel;
  pinion::PublisherHandle no_cond_publisher;

  InMemoryChannel<typename OutputPolicy::MsgType, OutputPolicy::channel_size> output_channel;
  InMemoryChannel<
    typename TestCogPolicy::CogTelemetryMetricsPolicy::MsgType,
    TestCogPolicy::CogTelemetryMetricsPolicy::channel_size>
    telemetry_channel;
  InMemoryChannel<
    typename TestCogPolicy::CogEventMetricsPolicy::MsgType,
    TestCogPolicy::CogEventMetricsPolicy::channel_size>
    event_channel;
  pinion::SubscriberHandle subscriber;
  pinion::SubscriberHandle telemetry_subscriber;
  pinion::SubscriberHandle event_subscriber;

  InMemoryChannel<typename diagnostics::ReportTap, OutputPolicy::channel_size> diagnostics_channel;
};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)  For testing purposes only
TEST_CASE_METHOD(SimpleCogFixture, "execution", "[simple_cog]")
{
  REQUIRE(cog.get_instance_id() == instance_id);

  auto memory_resource = std::make_shared<jewels::memory::MemoryResource>(std::pmr::new_delete_resource());
  REQUIRE(cog.set_handle(MemoryResourcePolicy::endpoint_id, *memory_resource));
  REQUIRE_FALSE(cog.validate());

  auto config = std::make_shared<const CogConfigDataImpl<TestConfig>>(TestConfig{.value = 4});
  REQUIRE(cog.set_handle(ConfigPolicy::endpoint_id, config));
  REQUIRE_FALSE(cog.validate());

  auto state = std::make_shared<CogStateDataImpl<TestState>>(*memory_resource);
  REQUIRE(cog.set_handle(StatePolicy::endpoint_id, state, true));
  REQUIRE_FALSE(cog.validate());

  auto maybe_timer_observer = cog.set_handle(TimerPolicy::endpoint_id, timer);
  REQUIRE(maybe_timer_observer);
  REQUIRE_FALSE(cog.validate());

  auto maybe_input_observer = cog.set_handle(InputPolicy::endpoint_id, input_channel.make_subscriber());
  REQUIRE(maybe_input_observer);
  REQUIRE_FALSE(cog.validate());

  auto maybe_no_cond_input_observer =
    cog.set_handle(NoCondInputPolicy::endpoint_id, no_cond_input_channel.make_subscriber());
  REQUIRE(maybe_no_cond_input_observer);
  REQUIRE_FALSE(cog.validate());

  REQUIRE(cog.set_handle(OutputPolicy::endpoint_id, output_channel.make_publisher(1), true));
  REQUIRE_FALSE(cog.validate());
  REQUIRE(
    cog.set_handle(TestCogPolicy::CogTelemetryMetricsPolicy::endpoint_id, telemetry_channel.make_publisher(1), true));
  REQUIRE_FALSE(cog.validate());
  REQUIRE(cog.set_handle(TestCogPolicy::CogEventMetricsPolicy::endpoint_id, event_channel.make_publisher(1), true));
  REQUIRE(cog.validate());

  auto& timer_observer = *maybe_timer_observer;
  auto& input_observer = *maybe_input_observer;

  // TODO(OI-1657): Update tests to include timers when we are using abstract clock

  SECTION("do not push onto queue when conditions are not met")
  {
    REQUIRE(queue.queue.empty());
    timer_observer->notify({.current_time = jewels::time::SyncTime{std::chrono::seconds{1}}});
    REQUIRE(queue.queue.empty());
    input_observer->notify({.current_time = jewels::time::SyncTime{std::chrono::seconds{1}}});
    REQUIRE(queue.queue.empty());
  }

  // Ensure the cog is pushed onto the queue when ready

  std::this_thread::sleep_for(2 * std::chrono::nanoseconds(TimerPolicy::threshold_ns));
  REQUIRE(queue.queue.empty());

  SECTION("Normal execution")
  {
    timer_observer->notify({});

    auto test_input = TestInput{.value = 1};
    publish(test_input);
    input_observer->notify({.current_time = jewels::time::SyncTime{std::chrono::seconds{1}}});
    REQUIRE(1 == queue.queue.size());

    // Execute the cog

    REQUIRE(cog.prepare_for_execution(jewels::time::SyncTime{std::chrono::seconds{1}}));

    auto exec_params = CogExecuteParams{
      .start_time = jewels::time::SyncTime{std::chrono::seconds{1}},
    };

    REQUIRE_NOTHROW(cog.execute(exec_params));

    // Verify the user function is called

    auto available = subscriber.available();
    REQUIRE(1 == available.size());

    auto range = pinion::to_message_range<const TestOutput>(available);
    REQUIRE(range);
    REQUIRE(1 == range->size());
    const auto& actual = *range->begin();

    REQUIRE(1 == actual.value);
  }

  SECTION("Publishing metrics")
  {
    using namespace std::chrono_literals;
    constexpr std::array<std::chrono::milliseconds, 4> execution_times{500ms, 2000ms, 2500ms, 3600ms};

    for (const auto& execution_time : execution_times)

    {
      timer_observer->notify({});
      auto test_input = TestInput{.value = 1};
      publish(test_input);
      input_observer->notify({.current_time = jewels::time::SyncTime{execution_time}});

      // Execute the cog

      REQUIRE(cog.prepare_for_execution(jewels::time::SyncTime{execution_time}));

      auto exec_params = CogExecuteParams{
        .start_time = jewels::time::SyncTime{execution_time},
      };

      REQUIRE_NOTHROW(cog.execute(exec_params));

      // Verify telemetry metrics are published
      if (execution_time == 2000ms)
      {
        REQUIRE(telemetry_subscriber.available().size() == 1);
        REQUIRE(event_subscriber.available().size() == 1);
      }
      if (execution_time == 2500ms)
      {
        REQUIRE(telemetry_subscriber.available().size() == 1);
        REQUIRE(event_subscriber.available().size() == 1);
      }
      if (execution_time == 3600ms)
      {
        REQUIRE(telemetry_subscriber.available().size() == 2);
        REQUIRE(event_subscriber.available().size() == 2);
      }
    }
  }

  SECTION("Event Metrics always published after 10 executions")
  {
    for (auto i = 0; i < 10; ++i)
    {
      timer_observer->notify({});
      auto test_input = TestInput{.value = 1};
      publish(test_input);
      input_observer->notify({.current_time = jewels::time::SyncTime{std::chrono::seconds{1}}});

      // Execute the cog

      REQUIRE(cog.prepare_for_execution(jewels::time::SyncTime{std::chrono::seconds{1}}));

      auto exec_params = CogExecuteParams{
        .start_time = jewels::time::SyncTime{std::chrono::seconds{1}},
      };

      REQUIRE(telemetry_subscriber.available().empty());
      REQUIRE(event_subscriber.available().empty());
      REQUIRE_NOTHROW(cog.execute(exec_params));
    }

    // Verify telemetry metrics are not published and event metrics were only published once.
    REQUIRE(telemetry_subscriber.available().empty());
    REQUIRE(event_subscriber.available().size() == 1);
  }

  SECTION("Exception from cog")
  {
    timer_observer->notify({});

    publish(TestInput{.value = std::numeric_limits<uint32_t>::max()});
    input_observer->notify({.current_time = jewels::time::SyncTime{std::chrono::seconds{1}}});
    REQUIRE(1 == queue.queue.size());

    REQUIRE(cog.prepare_for_execution(jewels::time::SyncTime{std::chrono::seconds{1}}));

    REQUIRE_THROWS(cog.execute(CogExecuteParams{.start_time = jewels::time::SyncTime{std::chrono::seconds{1}}}));

    // All slots should be discarded for exceptions.
    REQUIRE(subscriber.available().empty());
  }
}

struct RateLimitedCogState
{
  explicit RateLimitedCogState(jewels::memory::MemoryResource /*unused*/) {}

  uint32_t counter = {};
};

struct RateLimitedCogDial // NOLINT(cppcoreguidelines-pro-type-member-init). Initialization is uneeded for the relevant
                          // test cases
{
  /// The time the Cog user function was called.
  jewels::time::SyncTime start_time;

  /// Resources
  struct MemoryResources
  {
    jewels::memory::MemoryResource memres;
  };
  MemoryResources resources;

  /// Configs
  struct Configs
  {
  };
  Configs configs;

  /// States
  struct States
  {
    jewels::memory::ObjectPtr<RateLimitedCogState> state;
  };
  States states;

  /// Inputs
  struct Inputs
  {
  };
  Inputs inputs;

  /// Outputs
  struct Outputs
  {
    pinion::Publishable<TestOutput> output1;
    pinion::Publishable<TestOutput> output2;
  };
  Outputs outputs;

  /// Conditions
  struct Conditions
  {
    TimeSinceLastExecCondition<1'000'000U> periodic;
  };
  Conditions conditions;

  /// Diagnostics
  diagnostics::ClockworkManager<diagnostics::SignalGroupId::fault_injector_a>::Reporter* diagnostics;
};

struct RateLimitedCogPolicy : testing::FakeCogPolicy<0, 2>
{
  static constexpr auto cog_id =
    jewels::Uuid<common::CogClassId>::from_string("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee").value();
  static constexpr auto name = "clockwork::RateLimitedCogPolicy";
  /// Memory Resources

  struct MemoryResourcePolicy
  {
    using MemoryResourceType = jewels::memory::MemoryResource;
    static constexpr auto endpoint_id =
      jewels::Uuid<common::EndpointClassId>::from_string("d68c4441-7c8f-4377-bccf-260e363cec15").value();
    static constexpr std::string_view name = "TestMemoryResourcePolicy";
  };

  using MemoryResourcesType = CogMemoryResources<MemoryResourcePolicy>;

  /// States

  struct StatePolicy
  {
    using StateType = RateLimitedCogState;
    static constexpr auto endpoint_id =
      jewels::Uuid<common::EndpointClassId>::from_string("1d284a01-6e54-4996-92cd-ec35cbe0dad7").value();
    static constexpr std::string_view name = "RateLimitedCogStatePolicy";
    static constexpr bool read_only = false;
  };
  using StatesType = CogStates<StatePolicy>;

  /// Timers

  struct TimeSinceLastExecPolicy
  {
    static constexpr int64_t threshold_ns = 1'000'000; // 1 ms
    static constexpr auto endpoint_id =
      jewels::Uuid<common::EndpointClassId>::from_string("52e3bdf6-ef23-4053-bce3-1789b938ce6a").value();
    static constexpr std::string_view name = "TimeSinceLastExecPolicy";
  };

  using TimersType = CogTimers<TimeSinceLastExecPolicy>;

  /// Publishers

  struct Output1Policy
  {
    using MsgType = TestOutput;
    static constexpr auto endpoint_id =
      jewels::Uuid<common::EndpointClassId>::from_string("b6e2b628-62ba-4c73-b07e-b2ce77a742b4").value();
    static constexpr std::string_view name = "PublisherPolicy1";
    static constexpr std::optional<clockwork::RateLimitParameters> rate_limit_params{};
    // Testing only
    static constexpr auto channel_size = 3U;
  };

  struct Output2Policy
  {
    using MsgType = TestOutput;
    static constexpr auto endpoint_id =
      jewels::Uuid<common::EndpointClassId>::from_string("789e340c-556f-4cf0-a3b9-73632ba75a7c").value();
    static constexpr std::string_view name = "PublisherPolicy2";
    static constexpr std::optional<clockwork::RateLimitParameters> rate_limit_params{
      {.limit = 1U, .period = std::chrono::seconds{1}}};
    // Testing only
    static constexpr auto channel_size = 3U;
  };

  using PublishersType = CogPublishers<Output1Policy, Output2Policy>;

  /// Check the cog conditions for readiness.
  /// @param[in] timers The set of timer conditions
  /// @param[in] conditions The set of subscriber conditions
  /// @return True if the execution conditions are met
  [[nodiscard]] static bool is_ready(
    CogStatistics& /*statistics*/,
    typename TimersType::ConditionsTuple& timers,
    typename ConditionsType::ConditionsTuple& /*conditions*/)
  {
    return static_cast<bool>(std::get<0>(timers));
  }

  /// Make the dial inputs for the cog
  // NOLINTNEXTLINE(readability-function-size) Needs to match the signature of the make_dial function
  [[nodiscard]] static RateLimitedCogDial make_dial(
    const CogExecuteParams& params,
    const typename MemoryResourcesType::MemoryResourcesTuple& resources,
    const typename ConfigsType::ConfigsTuple& /*configs*/,
    const typename StatesType::StatesTuple& states,
    typename InputsType::InputDialTuple /*inputs*/,
    typename PublishersType::PublishablesTuple publishables,
    typename TimersType::ConditionsTuple& timer_conditions,
    typename ConditionsType::ConditionsTuple& /*input_conditions*/,
    typename DiagnosticsType::ReporterType& /*diagnostics*/)
  {
    return RateLimitedCogDial{
      .start_time = params.start_time,
      .resources =
        {
          .memres = std::get<0>(resources),
        },
      .configs = {},
      .states =
        {
          .state = std::get<0>(states),
        },
      .inputs = {},
      .outputs =
        {
          .output1 = std::move(std::get<0>(publishables)),
          .output2 = std::move(std::get<1>(publishables)),
        },
      .conditions =
        {
          .periodic = std::get<0>(timer_conditions),
        },
      .diagnostics = nullptr};
  }

  /// Execute the user defined function
  static void execute(RateLimitedCogDial dial)
  {
    auto counter = ++dial.states.state->counter;
    if (counter % 2 == 1)
    {
      auto& output = dial.outputs.output1.message();
      output = TestOutput{.value = counter};
      dial.outputs.output1.mark_for_publish();
    }
    else
    {
      auto& output = dial.outputs.output2.message();
      output = TestOutput{.value = counter};
      dial.outputs.output2.mark_for_publish();
    }
  }
};

using RateLimitedCog = SimpleCog<RateLimitedCogPolicy>;

struct RateLimitedCogFixture // NOLINT(clang-analyzer-optin.performance.Padding) Test code so performance is not a
                             // concern.
{
  using MemoryResourcePolicy = typename RateLimitedCogPolicy::MemoryResourcePolicy;
  using StatePolicy = typename RateLimitedCogPolicy::StatePolicy;
  using TimerPolicy = typename RateLimitedCogPolicy::TimeSinceLastExecPolicy;
  using Output1Policy = typename RateLimitedCogPolicy::Output1Policy;
  using Output2Policy = typename RateLimitedCogPolicy::Output2Policy;

  RateLimitedCogFixture()
    : resource(std::pmr::new_delete_resource()),
      instance_id(jewels::Uuid<common::CogInstanceId>::random_uuid()),
      cog(resource, instance_id, jewels::memory::make_non_null_from_ref(queue)),
      timer(std::make_shared<TestTimer>()),
      output1_channel(resource),
      output2_channel(resource),
      subscriber1(output1_channel.make_subscriber()),
      subscriber2(output2_channel.make_subscriber())
  {
  }

  jewels::memory::MemoryResource resource;
  clockwork::TestCogQueue queue;
  jewels::Uuid<common::CogInstanceId> instance_id;
  RateLimitedCog cog;

  std::shared_ptr<TestTimer> timer;

  InMemoryChannel<typename Output1Policy::MsgType, Output1Policy::channel_size> output1_channel;
  InMemoryChannel<typename Output2Policy::MsgType, Output2Policy::channel_size> output2_channel;
  pinion::SubscriberHandle subscriber1;
  pinion::SubscriberHandle subscriber2;
};


/// First, set up a SimpleCog execution fixture for a simple periodic cog with two outputs: one of which has a rate
/// limit.
TEST_CASE_METHOD(RateLimitedCogFixture, "rate limtied execution", "[rate limited]")
{
  auto memory_resource = std::make_shared<jewels::memory::MemoryResource>(std::pmr::new_delete_resource());
  REQUIRE(cog.set_handle(MemoryResourcePolicy::endpoint_id, *memory_resource));
  REQUIRE_FALSE(cog.validate());

  auto states = std::make_shared<CogStateDataImpl<RateLimitedCogState>>(*memory_resource);
  REQUIRE(cog.set_handle(StatePolicy::endpoint_id, states, true));
  REQUIRE_FALSE(cog.validate());

  auto maybe_timer_observer = cog.set_handle(TimerPolicy::endpoint_id, timer);
  REQUIRE(maybe_timer_observer);
  REQUIRE_FALSE(cog.validate());

  REQUIRE(cog.set_handle(Output1Policy::endpoint_id, output1_channel.make_publisher(1), true));
  REQUIRE(cog.set_handle(Output2Policy::endpoint_id, output2_channel.make_publisher(1), true));
  REQUIRE(cog.validate());

  auto& timer_observer = *maybe_timer_observer;

  REQUIRE(queue.queue.empty());
  timer_observer->notify({});
  REQUIRE(1 == queue.queue.size());

  // Step 1: Execute the cog
  jewels::time::SyncTime now{std::chrono::seconds{1}};
  REQUIRE(cog.prepare_for_execution(now));
  auto exec_params = CogExecuteParams{.start_time = now};
  REQUIRE_NOTHROW(cog.execute(exec_params));

  // The cog will alrenrate publishing on the two outputs. Whenever it produces
  // output on the second channel, it should be throttled. If it publishes on
  // the first, it should be able to execute again right away.
  // Step 2: Confirm that the cog published to the unlimited output.
  auto available1 = subscriber1.available();
  auto available2 = subscriber2.available();
  REQUIRE(1 == available1.size());
  REQUIRE(available2.empty());
  timer_observer->notify({});
  now += std::chrono::milliseconds(1);
  REQUIRE(cog.prepare_for_execution(now));
  exec_params = CogExecuteParams{.start_time = now};
  // Step 3: Confirm that the cog can execute again right away and that it publishes to the limited output.
  REQUIRE_NOTHROW(cog.execute(exec_params));
  REQUIRE(subscriber1.available().begin() == available1.begin());
  REQUIRE(subscriber1.available().end() == available1.end());
  available2 = subscriber2.available();
  REQUIRE(1 == available2.size());

  // Step 4: Try to execute the cog again. Confirm that it fails because it's been throttled.
  timer_observer->notify({});
  now += std::chrono::milliseconds(1);
  auto prepare_result = cog.prepare_for_execution(now);
  REQUIRE_FALSE(prepare_result);
  REQUIRE(prepare_result.error() == CogExecutionError::not_ready);
  timer_observer->notify({});

  // Step 5: Advance the clock by 1 millisecond and confirm that the cog is still throttled.
  timer_observer->notify({});
  now += std::chrono::milliseconds(1);
  prepare_result = cog.prepare_for_execution(now);
  REQUIRE_FALSE(prepare_result);
  REQUIRE(prepare_result.error() == CogExecutionError::not_ready);

  // Step 6: Advance the clock enough to clear the rate limit (998 ms) and confirm that the cog successfully executes.
  timer_observer->notify({});
  now += std::chrono::milliseconds(998);
  REQUIRE(cog.prepare_for_execution(now));
  exec_params = CogExecuteParams{.start_time = now};
  REQUIRE_NOTHROW(cog.execute(exec_params));

  // Step 7: Repeat steps 3, 4, and 6.
  timer_observer->notify({});
  now += std::chrono::milliseconds(1);
  REQUIRE(cog.prepare_for_execution(now));
  exec_params = CogExecuteParams{.start_time = now};
  REQUIRE_NOTHROW(cog.execute(exec_params));

  timer_observer->notify({});
  now += std::chrono::milliseconds(1);
  prepare_result = cog.prepare_for_execution(now);
  REQUIRE_FALSE(prepare_result);
  REQUIRE(prepare_result.error() == CogExecutionError::not_ready);

  timer_observer->notify({});
  now += std::chrono::milliseconds(998);
  REQUIRE(cog.prepare_for_execution(now));
  exec_params = CogExecuteParams{.start_time = now};
  REQUIRE_NOTHROW(cog.execute(exec_params));
}

// Initialization is uneeded for the relevant tests.
// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
struct SlowCogDial
{
  /// The time the Cog user function was called.
  jewels::time::SyncTime start_time;

  /// Resources
  struct MemoryResources
  {
    jewels::memory::MemoryResource memres;
  };
  MemoryResources resources;

  /// Configs
  struct Configs
  {
  };
  Configs configs;

  /// States
  struct States
  {
  };
  States states;

  /// Inputs
  struct Inputs
  {
    MessageInputDial<TestInput, 1> input;
  };
  Inputs inputs;

  /// Outputs
  struct Outputs
  {
  };
  Outputs outputs;

  /// Conditions
  struct Conditions
  {
    TimeSinceLastExecCondition<1'000'000U> periodic;
  };
  Conditions conditions;

  /// Diagnostics
  diagnostics::ClockworkManager<diagnostics::SignalGroupId::fault_injector_a>::Reporter* diagnostics;
};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) For testing purposes only
std::mutex exec_mutex;
// NOLINTNEXTLINE(fuchsia-statically-constructed-objects, cppcoreguidelines-avoid-non-const-global-variables) Testing
std::condition_variable cvar;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) For testing purposes only
bool cog_running{false};
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) For testing purposes only
bool can_exit{false};

struct SlowCogPolicy : testing::FakeCogPolicy<1, 0>
{
  static constexpr auto cog_id =
    jewels::Uuid<common::CogClassId>::from_string("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee").value();
  static constexpr auto name = "clockwork::SlowCogPolicy";
  /// Memory Resources

  struct MemoryResourcePolicy
  {
    using MemoryResourceType = jewels::memory::MemoryResource;
    static constexpr auto endpoint_id =
      jewels::Uuid<common::EndpointClassId>::from_string("d68c4441-7c8f-4377-bccf-260e363cec15").value();
    static constexpr std::string_view name = "TestMemoryResourcePolicy";
  };

  using MemoryResourcesType = CogMemoryResources<MemoryResourcePolicy>;

  /// Inputs

  struct InputPolicy
  {
    using MsgType = TestInput;
    static constexpr auto endpoint_id =
      jewels::Uuid<common::EndpointClassId>::from_string("7ccf2fc5-b99d-4a4b-8405-27651832c81a").value();
    static constexpr std::string_view name = "InputPolicy";
    static constexpr auto max_view_size = 1U;
    static constexpr std::optional<::ssize_t> safety_margin{};
    static constexpr std::optional<size_t> skip_threshold{};
    static constexpr auto copy_inputs = false;
    static constexpr auto manual_cursor = false;
    // Testing only
    static constexpr auto channel_size = 3U;
  };

  using InputsType = CogInputs<InputPolicy>;

  /// Timers

  struct TimeSinceLastExecPolicy
  {
    static constexpr int64_t threshold_ns = 1'000'000; // 1 ms
    static constexpr auto endpoint_id =
      jewels::Uuid<common::EndpointClassId>::from_string("4887531c-de8b-4aa8-a64b-61ee17ed92c3").value();
    static constexpr std::string_view name = "TimeSinceLastExecPolicy";
  };

  using TimersType = CogTimers<TimeSinceLastExecPolicy>;

  /// Check the cog conditions for readiness.
  /// @param[in] timers The set of timer conditions
  /// @param[in] conditions The set of subscriber conditions
  /// @return True if the execution conditions are met
  [[nodiscard]] static bool is_ready(
    CogStatistics& /*statistics*/,
    typename TimersType::ConditionsTuple& timers,
    typename ConditionsType::ConditionsTuple& /*conditions*/)
  {
    return static_cast<bool>(std::get<0>(timers));
  }

  /// Make the dial inputs for the cog
  // NOLINTNEXTLINE(readability-function-size) Needs to match the signature of the make_dial function
  [[nodiscard]] static SlowCogDial make_dial(
    const CogExecuteParams& params,
    const typename MemoryResourcesType::MemoryResourcesTuple& resources,
    const typename ConfigsType::ConfigsTuple& /*configs*/,
    const typename StatesType::StatesTuple& /*states*/,
    typename InputsType::InputDialTuple inputs,
    typename PublishersType::PublishablesTuple /*publishables*/,
    typename TimersType::ConditionsTuple& timer_conditions,
    typename ConditionsType::ConditionsTuple& /*input_conditions*/,
    typename DiagnosticsType::ReporterType& /*diagnostics*/)
  {
    return SlowCogDial{
      .start_time = params.start_time,
      .resources =
        {
          .memres = std::get<0>(resources),
        },
      .configs = {},
      .states = {},
      .inputs =
        {
          .input = std::get<0>(inputs),
        },
      .outputs = {},
      .conditions =
        {
          .periodic = std::get<0>(timer_conditions),
        },
      .diagnostics = nullptr};
  }

  /// Execute the user defined function
  static void execute(SlowCogDial /*dial*/)
  {
    {
      const std::scoped_lock lock(exec_mutex);
      cog_running = true;
    }
    cvar.notify_one();
    std::unique_lock lock(exec_mutex);
    cvar.wait(lock, [] { return can_exit; });
  }
};

using SlowCog = SimpleCog<SlowCogPolicy>;

// This is test code, so performance is not a concern.
// NOLINTNEXTLINE(clang-analyzer-optin.performance.Padding)
struct SlowCogFixture
{
  using MemoryResourcePolicy = typename SlowCogPolicy::MemoryResourcePolicy;
  using TimerPolicy = typename SlowCogPolicy::TimeSinceLastExecPolicy;
  using InputPolicy = typename SlowCogPolicy::InputPolicy;

  SlowCogFixture()
    : resource(std::pmr::new_delete_resource()),
      instance_id(jewels::Uuid<common::CogInstanceId>::random_uuid()),
      cog(resource, instance_id, jewels::memory::make_non_null_from_ref(queue)),
      timer(std::make_shared<TestTimer>()),
      input_channel(resource),
      publisher(input_channel.make_publisher(1))
  {
  }

  void publish(TestInput msg)
  {
    auto slot = publisher.reserve().value();
    auto publishable = pinion::Publishable<TestInput>::try_make(jewels::memory::make_non_null_from_ref(slot)).value();
    publishable.message() = msg;
    REQUIRE(slot.commit(fake_publish_time));
  }

  jewels::memory::MemoryResource resource;
  clockwork::TestCogQueue queue;
  jewels::Uuid<common::CogInstanceId> instance_id;
  SlowCog cog;

  std::shared_ptr<TestTimer> timer;

  InMemoryChannel<typename InputPolicy::MsgType, InputPolicy::channel_size> input_channel;
  pinion::PublisherHandle publisher;
};

TEST_CASE_METHOD(SlowCogFixture, "Overrun Aborts")
{
  auto memory_resource = std::make_shared<jewels::memory::MemoryResource>(std::pmr::new_delete_resource());
  REQUIRE(cog.set_handle(MemoryResourcePolicy::endpoint_id, *memory_resource));
  REQUIRE_FALSE(cog.validate());

  auto maybe_timer_observer = cog.set_handle(TimerPolicy::endpoint_id, timer);
  REQUIRE(maybe_timer_observer);
  REQUIRE_FALSE(cog.validate());

  auto maybe_input_observer = cog.set_handle(SlowCogPolicy::InputPolicy::endpoint_id, input_channel.make_subscriber());
  REQUIRE(maybe_input_observer);
  REQUIRE(cog.validate());

  auto& timer_observer = *maybe_timer_observer;
  auto& input_observer = *maybe_input_observer;

  REQUIRE(queue.queue.empty());
  timer_observer->notify({});


  // First, set up a SimpleCog execution fixture for a simple periodic cog with
  // a single input. The view should hold one message and the channel should
  // hold three messages. The cog function should coordinate with a mock
  // infrastructure thread using a condition variable and a pair of flags. See
  // SlowCogPolicy::execute() above for details.
  SECTION("Infra Thread Detection")
  {
    can_exit = false;
    cog_running = false;

    timer_observer->notify({});
    auto test_input = TestInput{.value = 1};
    publish(test_input);
    const jewels::time::SyncTime exec_time{std::chrono::seconds{1}};
    input_observer->notify({.current_time = exec_time});

    // Step 1: Spawn a background thread to execute the cog.
    std::thread mock_worker(
      [this, exec_time]()
      {
        // Step 2 [from bg thread]: Execute the cog.
        REQUIRE(cog.prepare_for_execution(exec_time));
        auto exec_params = CogExecuteParams{.start_time = exec_time};
        REQUIRE_NOTHROW(cog.execute(exec_params));
      });

    // Step 3: Wait for the cog the signal that it has reached the cog user
    // function.
    std::unique_lock lock(exec_mutex);
    cvar.wait(lock, [] { return cog_running; });

    // Step 4: Before triggering the infrastructure termination code, override
    // the termination handler with a function that sets a flag.
    auto old_handler = std::set_terminate(&terminate_handler);
    bool terminated{false};
    // NOLINTNEXTLINE(cert-err52-cpp, cppcoreguidelines-pro-bounds-array-to-pointer-decay) For testing purposes only
    if (setjmp(terminate_handler_jmp))
    {
      // This arm is only used by the termination handler. The other arm is
      // taken by the original caller. When it triggers a termination, it
      // will jump here.
      terminated = true;
    }
    else
    {
      // Step 5: Publish one message to the channel and notify the cog observer.
      // This should push the producer position within the cog's emergency
      // margin.
      auto spam = TestInput{.value = 99};
      publish(spam);
      input_observer->notify({.current_time = jewels::time::SyncTime{std::chrono::seconds{2}}});
      REQUIRE(false); // should never get here
    }

    // Step 6: Check that the infrastructure notification hit the terminate
    // handler and set the flag from step 4.
    REQUIRE(terminated);
    std::ignore = std::set_terminate(old_handler);

    // Step 7: Signal to the cog that it can exit.
    can_exit = true;
    lock.unlock();
    cvar.notify_one();
    mock_worker.join();
  }


  // First, set up a SimpleCog execution fixture for a simple periodic cog with
  // a single input. The view should hold one message and the channel should
  // hold three messages. The cog function should coordinate with a mock
  // infrastructure thread using a condition variable and a pair of flags. See
  // SlowCogPolicy::execute() above for details.
  SECTION("Execution Thread Detection")
  {
    can_exit = false;
    cog_running = false;

    timer_observer->notify({});
    auto test_input = TestInput{.value = 1};
    publish(test_input);
    const jewels::time::SyncTime exec_time{std::chrono::seconds{1}};
    input_observer->notify({.current_time = exec_time});

    // Step 1: Spawn a background thread to execute the cog.
    bool terminated{false};
    std::thread mock_worker(
      [this, exec_time, &terminated]()
      {
        // Step 2 [from bg thread]: Before executing the cog, override the
        // termination handler with a function that sets a flag.
        auto old_handler = std::set_terminate(&terminate_handler);
        // NOLINTNEXTLINE(cert-err52-cpp, cppcoreguidelines-pro-bounds-array-to-pointer-decay) For testing purposes only
        if (setjmp(terminate_handler_jmp))
        {
          // This arm is only used by the termination handler. The other arm is
          // taken by the original caller. When it triggers a termination, it
          // will jump here.
          terminated = true;
        }
        else
        {
          // Step 3 [from bg thread]: Execute the cog.
          REQUIRE(cog.prepare_for_execution(exec_time));
          auto exec_params = CogExecuteParams{.start_time = exec_time};
          REQUIRE_NOTHROW(cog.execute(exec_params));
          REQUIRE(false); // should never get here
        }
        std::ignore = std::set_terminate(old_handler);
      });

    // Step 4: Wait for the cog the signal that it has reached the cog user
    // function.
    std::unique_lock lock(exec_mutex);
    cvar.wait(lock, [] { return cog_running; });

    // Step 5: Publish three messages to the channel. This should overrun the
    // cog.
    auto spam = TestInput{.value = 99};
    publish(spam);
    publish(spam);
    publish(spam);

    // Step 6: Signal to the cog that it can exit.
    can_exit = true;
    lock.unlock();
    cvar.notify_one();
    mock_worker.join();
    // Step 7: Check that the cog hit the terminate handler and set the flag
    // from step 2.
    REQUIRE(terminated);
  }
}

} // namespace
} // namespace clockwork
