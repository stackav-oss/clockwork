// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/cog_conditions.hh"
#include "clockwork/cog/cog_configs.hh"
#include "clockwork/cog/cog_diagnostics.hh"
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
#include "clockwork/common/abstract_cog.hh"
#include "clockwork/common/abstract_cog_queue.hh"
#include "clockwork/common/abstract_timer.hh"
#include "clockwork/common/cog_envelope.hh"
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

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iterator>
#include <limits>
#include <memory>
#include <memory_resource>
#include <optional>
#include <ranges>
#include <ratio>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>

namespace clockwork
{
namespace
{

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
    const TestConfig& test_configs; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members) Needs to be a reference
                                    // for the purpose of this test.
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
  static constexpr auto simulated_execution_duration = std::chrono::milliseconds(0);
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

  /// Publishers

  struct TestOutputPolicy
  {
    using MsgType = TestOutput;
    [[maybe_unused]] static constexpr auto endpoint_id =
      jewels::Uuid<common::EndpointClassId>::from_string("23456701-ab89-efcd-dcfe-9876543210ba").value();
    static constexpr std::string_view name = "TestOutputPolicy";
    // Testing only
    static constexpr auto channel_size = 3U;
  };

  using PublishersType = CogPublishers<TestOutputPolicy>;

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
        {
          .test_output = std::move(std::get<0>(publishables)),
        },
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
      subscriber(output_channel.make_subscriber()),
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
  pinion::SubscriberHandle subscriber;

  InMemoryChannel<typename diagnostics::ReportTap, OutputPolicy::channel_size> diagnostics_channel;
};

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

  REQUIRE(cog.set_handle(OutputPolicy::endpoint_id, output_channel.make_publisher(1)));
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

} // namespace
} // namespace clockwork
