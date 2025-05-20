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
#include "clockwork/common/process_description.hh"
#include "clockwork/dial/cond_messages_present.hh"
#include "clockwork/dial/cond_time_since_last_exec.hh"
#include "clockwork/dial/msg_input.hh"
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/in_memory_channel.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "clockwork/runners/epoll_manager.hh"
#include "clockwork/runners/online_cog_queue.hh"
#include "clockwork/runners/online_runner.hh"
#include "clockwork/runners/thread_pool.hh"
#include "clockwork/runners/timerfd_timer.hh"
#include "jewels/container/circular_buffer.hh"
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
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <ranges>
#include <string_view>
#include <sys/epoll.h>
#include <thread>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace clockwork
{
namespace
{

struct PublisherState
{
  explicit PublisherState(jewels::memory::MemoryResource /*resource*/) {}
  int32_t sequence_number = {};
};

struct SequenceMsg
{
  int32_t sequence_number = {};
};

struct PublisherCogDial
{
  jewels::time::SyncTime start_time;
  jewels::memory::ObjectPtr<PublisherState> state;
  jewels::memory::ObjectPtr<pinion::Publishable<SequenceMsg>> output;
};

/// An periodic publisher cog for testing multiple subscribers
struct PublisherCogPolicy
{
  static constexpr auto cog_id =
    jewels::Uuid<common::EndpointClassId>::from_string("798d020b-40e2-4482-9b9a-e18d9d410697").value();
  static constexpr auto name = "clockwork::PublisherCogPolicy";
  static constexpr auto simulated_execution_duration = std::chrono::milliseconds(0);

  using MemoryResourcesType = CogMemoryResources<>;
  using ConfigsType = CogConfigs<>;
  using InputsType = CogInputs<>;
  using ConditionsType = CogConditions<>;
  using DiagnosticsType = CogDiagnostics<>;

  struct StatePolicy
  {
    using StateType = PublisherState;
    static constexpr auto name = "StatePolicy";
    static constexpr auto endpoint_id =
      jewels::Uuid<common::EndpointClassId>::from_string("5345432e-8758-4c0e-8fac-0f7bf36b7947").value();
    static constexpr bool read_only = false;
  };
  using StatesType = CogStates<StatePolicy>;

  struct TimerPolicy
  {
    static constexpr int64_t threshold_ns = 1'000'000; // 1 ms
    static constexpr auto endpoint_id =
      jewels::Uuid<common::EndpointClassId>::from_string("c2d10425-8974-4a22-89f0-52507aedf53e").value();
    static constexpr std::string_view name = "TimerPolicy";
  };
  using TimersType = CogTimers<TimerPolicy>;

  struct OutputPolicy
  {
    using MsgType = SequenceMsg;
    [[maybe_unused]] static constexpr auto endpoint_id =
      jewels::Uuid<common::EndpointClassId>::from_string("64386888-7392-45a0-a45f-28696f3d3c22").value();
    static constexpr std::string_view name = "PublisherPolicy";
  };
  using PublishersType = CogPublishers<OutputPolicy>;

  [[nodiscard]] static bool is_ready(
    CogStatistics& /*statistics*/,
    typename TimersType::ConditionsTuple& timers,
    typename ConditionsType::ConditionsTuple& /*conditions*/)
  {
    return std::get<0>(timers).is_active();
  }

  // NOLINTNEXTLINE(readability-function-size) Needs to match the signature of the make_dial function
  [[nodiscard]] static PublisherCogDial make_dial(
    const CogExecuteParams& params,
    const typename MemoryResourcesType::MemoryResourcesTuple& /*resources*/,
    const typename ConfigsType::ConfigsTuple& /*configs*/,
    const typename StatesType::StatesTuple& states,
    typename InputsType::InputDialTuple /*inputs*/,
    typename PublishersType::PublishablesTuple publishables,
    typename TimersType::ConditionsTuple& /*timer_conditions*/,
    typename ConditionsType::ConditionsTuple& /*input_conditions*/,
    typename DiagnosticsType::ReporterType& /*diagnostics*/)
  {
    return PublisherCogDial{
      .start_time = params.start_time,
      .state = jewels::memory::make_non_null_from_ref(*std::get<0>(states)),
      .output = jewels::memory::make_non_null_from_ref(std::get<0>(publishables)),
    };
  }

  static void execute(PublisherCogDial& dial)
  {
    ++dial.state->sequence_number;
    dial.output->message().sequence_number = dial.state->sequence_number;
    dial.output->mark_for_publish();
  }
};

struct SubscriberState
{
  explicit SubscriberState(jewels::memory::MemoryResource /*resource*/) {}
  int32_t count = {};
  int32_t skips = {};
  int32_t last_sequence_number = {};
};

struct SubscriberCogDial
{
  jewels::time::SyncTime start_time;
  jewels::memory::ObjectPtr<SubscriberState> state;
  jewels::memory::ObjectPtr<const MessageInputDial<SequenceMsg, 1>> input;
};

/// An periodic publisher cog for testing multiple subscribers
struct SubscriberCogPolicy
{
  static constexpr auto cog_id =
    jewels::Uuid<common::EndpointClassId>::from_string("4475c83e-9318-424b-a5df-0026cf9e9496").value();
  static constexpr auto name = "clockwork::SubscriberCogPolicy";
  static constexpr auto simulated_execution_duration = std::chrono::milliseconds(0);

  using MemoryResourcesType = CogMemoryResources<>;
  using ConfigsType = CogConfigs<>;
  using DiagnosticsType = CogDiagnostics<>;
  using TimersType = CogTimers<>;
  using PublishersType = CogPublishers<>;

  struct StatePolicy
  {
    using StateType = SubscriberState;
    static constexpr auto name = "StatePolicy";
    static constexpr auto endpoint_id =
      jewels::Uuid<common::EndpointClassId>::from_string("16a70fb0-f94b-455f-b91f-9b2d07404aae").value();
    static constexpr bool read_only = false;
  };

  using StatesType = CogStates<StatePolicy>;

  struct ConditionPolicy
  {
    using MsgType = SequenceMsg;
    static constexpr auto endpoint_id =
      jewels::Uuid<common::EndpointClassId>::from_string("ac2c14ce-b909-4793-af02-7d2dbe27a2b1").value();
    static constexpr std::string_view name = "ConditionPolicy";
    static constexpr auto bounds_min = 1U;
    static constexpr auto bounds_max = 1U;
    static constexpr auto condition_type = InputConditionType::new_message;
  };

  using ConditionsType = CogConditions<ConditionPolicy>;

  struct InputPolicy
  {
    using MsgType = SequenceMsg;
    static constexpr auto endpoint_id =
      jewels::Uuid<common::EndpointClassId>::from_string("ac2c14ce-b909-4793-af02-7d2dbe27a2b1").value();
    static constexpr std::string_view name = "InputPolicy";
    static constexpr auto max_view_size = 1U;
    static constexpr auto copy_inputs = false;
    static constexpr auto manual_cursor = false;
  };

  using InputsType = CogInputs<InputPolicy>;

  [[nodiscard]] static bool is_ready(
    CogStatistics& /*statistics*/,
    typename TimersType::ConditionsTuple& /*timers*/,
    typename ConditionsType::ConditionsTuple& conditions)
  {
    return std::get<0>(conditions).is_active();
  }

  // NOLINTNEXTLINE(readability-function-size) Needs to match the signature of the make_dial function.
  [[nodiscard]] static SubscriberCogDial make_dial(
    const CogExecuteParams& params,
    const typename MemoryResourcesType::MemoryResourcesTuple& /*resources*/,
    const typename ConfigsType::ConfigsTuple& /*configs*/,
    const typename StatesType::StatesTuple& states,
    typename InputsType::InputDialTuple inputs,
    typename PublishersType::PublishablesTuple /*publishables*/,
    typename TimersType::ConditionsTuple& /*timer_conditions*/,
    typename ConditionsType::ConditionsTuple& /*input_conditions*/,
    typename DiagnosticsType::ReporterType& /*diagnostics*/)
  {
    return SubscriberCogDial{
      .start_time = params.start_time,
      .state = jewels::memory::make_non_null_from_ref(*std::get<0>(states)),
      .input = jewels::memory::make_non_null_from_ref(std::get<0>(inputs)),
    };
  }

  static void execute(SubscriberCogDial& dial)
  {
    for (const auto& msg : dial.input->get_new_msgs_view())
    {
      ++dial.state->count;
      dial.state->skips += ((msg.sequence_number - dial.state->last_sequence_number) - 1);
      dial.state->last_sequence_number = msg.sequence_number;
    }
  }
};

TEST_CASE("multiple subscribers", "[simple_cog]")
{
  constexpr auto duration = std::chrono::seconds(5);
  constexpr auto min_exe_count = 1;

  auto resource = jewels::memory::MemoryResource(std::pmr::new_delete_resource());
  auto epoll = EPollManager{resource};
  auto queue = OnlineCogQueue(resource);
  auto timer = std::make_shared<TimerfdTimer>();

  auto publisher_state = std::make_shared<CogStateDataImpl<PublisherState>>(resource);
  auto subscriber0_state = std::make_shared<CogStateDataImpl<SubscriberState>>(resource);
  auto subscriber1_state = std::make_shared<CogStateDataImpl<SubscriberState>>(resource);

  InMemoryChannel<SequenceMsg, 1000> channel(resource);

  auto publisher_cog = std::make_unique<SimpleCog<PublisherCogPolicy>>(
    resource, jewels::Uuid<common::CogInstanceId>{}, jewels::memory::make_non_null_from_ref(queue));
  auto subscriber0_cog = std::make_unique<SimpleCog<SubscriberCogPolicy>>(
    resource, jewels::Uuid<common::CogInstanceId>{}, jewels::memory::make_non_null_from_ref(queue));
  auto subscriber1_cog = std::make_unique<SimpleCog<SubscriberCogPolicy>>(
    resource, jewels::Uuid<common::CogInstanceId>{}, jewels::memory::make_non_null_from_ref(queue));

  auto timer_observer = publisher_cog->set_handle(PublisherCogPolicy::TimerPolicy::endpoint_id, timer);
  REQUIRE(timer_observer);
  timer->set_observer(timer_observer->get());
  REQUIRE(epoll.add(timer->descriptor(), EPOLLIN, timer));

  REQUIRE(publisher_cog->set_handle(PublisherCogPolicy::StatePolicy::endpoint_id, publisher_state, false));
  REQUIRE(subscriber0_cog->set_handle(SubscriberCogPolicy::StatePolicy::endpoint_id, subscriber0_state, false));
  REQUIRE(subscriber1_cog->set_handle(SubscriberCogPolicy::StatePolicy::endpoint_id, subscriber1_state, false));

  auto publisher = channel.make_publisher(2);

  auto subscriber0_observer =
    subscriber0_cog->set_handle(SubscriberCogPolicy::InputPolicy::endpoint_id, channel.make_subscriber());
  REQUIRE(subscriber0_observer);
  REQUIRE(publisher.add_observer(jewels::memory::make_non_null_from_ref(**subscriber0_observer)));

  auto subscriber1_observer =
    subscriber1_cog->set_handle(SubscriberCogPolicy::InputPolicy::endpoint_id, channel.make_subscriber());
  REQUIRE(subscriber1_observer);
  REQUIRE(publisher.add_observer(jewels::memory::make_non_null_from_ref(**subscriber1_observer)));

  REQUIRE(publisher_cog->set_handle(PublisherCogPolicy::OutputPolicy::endpoint_id, std::move(publisher)));

  REQUIRE(publisher_cog->validate());
  REQUIRE(subscriber0_cog->validate());
  REQUIRE(subscriber1_cog->validate());

  auto pool_config = ThreadPoolConfig{
    .resource = resource,
    .thread_configs =
      {
        {.work = jewels::memory::make_non_null_from_ref(queue)},
        {.work = jewels::memory::make_non_null_from_ref(queue)},
        {.work = jewels::memory::make_non_null_from_ref(epoll)},
      },
  };
  auto pool = ThreadPool(pool_config);

  auto runner_config = OnlineRunnerConfig{
    .cogs = std::pmr::vector<CogConfig>(
      {
        CogConfig{.cog = jewels::memory::make_non_null_from_ref(*publisher_cog)},
        CogConfig{.cog = jewels::memory::make_non_null_from_ref(*subscriber0_cog)},
        CogConfig{.cog = jewels::memory::make_non_null_from_ref(*subscriber1_cog)},
      },
      resource),
    .pool = jewels::memory::make_non_null_from_ref(pool)};
  auto runner = OnlineRunner(runner_config);

  // Notify all the cogs to start them.

  constexpr auto default_timer_timeout = std::chrono::milliseconds(50);
  REQUIRE(timer->start(jewels::time::SyncClock::now() + default_timer_timeout, default_timer_timeout));
  publisher_cog->notify({});

  // Start the runner.

  runner.start();

  // Wait a few seconds.

  std::this_thread::sleep_for(duration);

  // Stop the runner.

  runner.stop();
  runner.join();

  // Verify no cogs were starved.

  REQUIRE(publisher_state->state.sequence_number > min_exe_count);
  REQUIRE(subscriber0_state->state.count > min_exe_count);
  REQUIRE(subscriber1_state->state.count > min_exe_count);

  REQUIRE(subscriber0_state->state.skips == 0);
  REQUIRE(subscriber1_state->state.skips == 0);
}

} // namespace
} // namespace clockwork
