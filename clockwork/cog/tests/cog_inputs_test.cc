// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/cog_conditions.hh"
#include "clockwork/cog/cog_inputs.hh"
#include "clockwork/cog/input_condition.hh"
#include "clockwork/common/process_description.hh"
#include "clockwork/dial/cond_messages_present.hh"
#include "clockwork/dial/msg_input.hh"
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/in_memory_channel.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/subscriber_handle.hh"
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

#include <array>
#include <chrono>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory_resource>
#include <optional>
#include <ranges>
#include <string_view>
#include <sys/types.h>
#include <tuple>
#include <type_traits>
#include <utility>

namespace clockwork
{
namespace
{

/// Fixed timestamp for publish timestamp.
inline constexpr auto fake_publish_time{jewels::time::SyncTime{std::chrono::nanoseconds{12345}}};

struct TestMsg1
{
  int32_t value = {};
  bool operator==(const TestMsg1& rhs) const = default;
};

struct TestMsg2
{
  int32_t value = {};
  bool operator==(const TestMsg2& rhs) const = default;
};

class TestCog
{
public:
  static constexpr size_t event_metrics_batch_size = 10;
  void notify(jewels::time::SyncTime /*current_time*/) {}
};

template <typename... Types, typename... Args>
auto make_tuple_repeat(Args&... args)
{
  // NOLINTNEXTLINE(bugprone-use-after-move) TODO(DX-1794): Fix this
  return std::tuple<Types...>(Types{std::forward<Args...>(args...)}...);
}

template <typename InputType>
struct NewMin1Policy
{
  using MsgType = TestMsg1;
  static constexpr auto endpoint_id = InputType::endpoint_id;
  static constexpr std::string_view name = "NewInputPolicy";
  static constexpr auto bounds_min = 1U;
  static constexpr auto bounds_max = std::numeric_limits<uint32_t>::max();
  static constexpr auto condition_type = InputConditionType::new_message;
  // Testing only
  using ConditionType = MessagePresentCondition<bounds_min, bounds_max>;
  static ConditionType make_active_cond()
  {
    constexpr auto active = true;
    constexpr auto num_msgs = bounds_max;
    return ConditionType(active, num_msgs);
  }
};

template <typename... Policies>
struct CogInputsFixture // NOLINT(clang-analyzer-optin.performance.Padding) Test code performance is not a concern.
{
  static constexpr auto policy_count = sizeof...(Policies);
  using ChannelsTuple = std::tuple<InMemoryChannel<typename Policies::MsgType, Policies::max_view_size>...>;
  using PublishersArray = std::array<pinion::PublisherHandle, policy_count>;
  using ConditionsType = CogConditions<NewMin1Policy<Policies>...>;
  using ConditionsTuple = typename ConditionsType::ConditionsTuple;

  CogInputsFixture()
    : resource(std::pmr::new_delete_resource()),
      channels(make_tuple_repeat<InMemoryChannel<typename Policies::MsgType, Policies::max_view_size>...>(resource)),
      publishers(
        std::apply([](auto&... channel) -> PublishersArray { return {channel.make_publisher(1)...}; }, channels)),
      subscriber(resource, false)
  {
  }

  jewels::memory::MemoryResource resource;
  TestCog cog;
  ChannelsTuple channels;
  PublishersArray publishers;
  CogInputs<Policies...> subscriber;

  /// Publish the message and return a pointer to the published message.
  template <typename MsgType>
  void publish(pinion::PublisherHandle& handle, const MsgType& msg)
  {
    auto slot = handle.reserve().value();
    auto publishable = pinion::Publishable<MsgType>::try_make(jewels::memory::make_non_null_from_ref(slot)).value();
    publishable.message() = msg;
    REQUIRE(slot.commit(fake_publish_time));
  }

  static ConditionsTuple make_active_conds()
  {
    return ConditionsTuple(NewMin1Policy<Policies>::make_active_cond()...);
  }
};

struct NoCopyInputPolicy
{
  using MsgType = TestMsg1;
  static constexpr auto endpoint_id =
    jewels::Uuid<common::EndpointClassId>::from_string("b6e2b628-62ba-4c73-b07e-b2ce77a742b4").value();
  static constexpr std::string_view name = "NoCopyInputPolicy";
  static constexpr auto max_view_size = 3U;
  static constexpr std::optional<::ssize_t> safety_margin{};
  static constexpr std::optional<size_t> skip_threshold{};
  static constexpr auto copy_inputs = false;
  static constexpr auto manual_cursor = false;
};

struct CopyInputPolicy
{
  using MsgType = TestMsg2;
  static constexpr auto endpoint_id =
    jewels::Uuid<common::EndpointClassId>::from_string("789e340c-556f-4cf0-a3b9-73632ba75a7c").value();
  static constexpr std::string_view name = "CopyInputPolicy";
  static constexpr auto max_view_size = 3U;
  static constexpr std::optional<::ssize_t> safety_margin{};
  static constexpr std::optional<size_t> skip_threshold{};
  static constexpr auto copy_inputs = false;
  static constexpr auto manual_cursor = false;
};

using InputPolicyFixture = CogInputsFixture<NoCopyInputPolicy, CopyInputPolicy>;

TEST_CASE_METHOD(InputPolicyFixture, "basic operation", "[cog_inputs]")
{
  // Make publishers

  auto& publisher1 = publishers.at(0);
  auto& publisher2 = publishers.at(1);

  // Set subcriber handles

  SECTION("set_handle unknown id")
  {
    constexpr auto unknown_id =
      jewels::Uuid<common::EndpointClassId>::from_string("b5e2c9a9-e351-4877-b5cc-77e76a7ebe63").value();
    REQUIRE_FALSE(subscriber.set_handle(
      unknown_id, std::get<0>(channels).make_subscriber(), jewels::memory::make_non_null_from_ref(cog)));
  }

  REQUIRE(subscriber.set_handle(
    NoCopyInputPolicy::endpoint_id,
    std::get<0>(channels).make_subscriber(),
    jewels::memory::make_non_null_from_ref(cog)));
  REQUIRE(subscriber.set_handle(
    CopyInputPolicy::endpoint_id,
    std::get<1>(channels).make_subscriber(),
    jewels::memory::make_non_null_from_ref(cog)));
  REQUIRE(subscriber.validate());

  // Empty views before receiving any messages
  auto conds = make_active_conds();
  auto inputs = subscriber.make_dial_inputs<ConditionsType>(conds, fake_publish_time);
  REQUIRE(inputs);
  REQUIRE(std::get<0>(*inputs).get_cursor_view().empty());
  REQUIRE(std::get<1>(*inputs).get_cursor_view().empty());

  // Publish message to channel 1

  auto msg1 = TestMsg1{.value = 1};
  publish(publisher1, msg1);

  {
    inputs = subscriber.make_dial_inputs<ConditionsType>(conds, fake_publish_time);
    REQUIRE(inputs);
    REQUIRE(1 == std::get<0>(*inputs).get_cursor_view().size());
    REQUIRE(std::get<1>(*inputs).get_cursor_view().empty());
  }

  // Publish message to channel 2

  auto msg2 = TestMsg2{.value = 2};
  publish(publisher2, msg2);

  {
    inputs = subscriber.make_dial_inputs<ConditionsType>(conds, fake_publish_time);
    REQUIRE(inputs);

    const auto& input0 = std::get<0>(*inputs);
    REQUIRE(1 == input0.get_cursor_view().size());
    REQUIRE(input0.get_cursor() == input0.get_first_new());

    const auto& input1 = std::get<1>(*inputs);
    REQUIRE(1 == input1.get_cursor_view().size());
    REQUIRE(input1.get_cursor() == input1.get_first_new());
  }

  SECTION("update last consumed")
  {
    std::ignore = subscriber.commit(*inputs);

    inputs = subscriber.make_dial_inputs<ConditionsType>(conds, fake_publish_time);
    REQUIRE(inputs);

    const auto& input0 = std::get<0>(*inputs);
    REQUIRE(input0.get_cursor_view().empty());
    REQUIRE(input0.end() == input0.get_first_new());

    const auto& input1 = std::get<1>(*inputs);
    REQUIRE(input1.get_cursor_view().empty());
    REQUIRE(input1.end() == input1.get_first_new());
  }

  SECTION("overrun subscriber 1")
  {
    REQUIRE_FALSE(subscriber.is_overrun());
    REQUIRE_FALSE(subscriber.almost_overrun());

    publish(publisher1, msg1);
    REQUIRE(subscriber.almost_overrun());
    publish(publisher2, msg2);
    REQUIRE(subscriber.almost_overrun());

    inputs = subscriber.make_dial_inputs<ConditionsType>(conds, fake_publish_time);
    REQUIRE(inputs);

    publish(publisher1, msg1);
    publish(publisher1, msg1);
    publish(publisher1, msg1);
    publish(publisher1, msg1);

    REQUIRE(subscriber.is_overrun());
    REQUIRE(subscriber.almost_overrun());
  }

  SECTION("overrun subscriber 2")
  {
    REQUIRE_FALSE(subscriber.is_overrun());
    REQUIRE_FALSE(subscriber.almost_overrun());

    publish(publisher1, msg1);
    REQUIRE(subscriber.almost_overrun());
    publish(publisher2, msg2);
    REQUIRE(subscriber.almost_overrun());

    inputs = subscriber.make_dial_inputs<ConditionsType>(conds, fake_publish_time);
    REQUIRE(inputs);

    publish(publisher2, msg2);
    publish(publisher2, msg2);
    publish(publisher2, msg2);
    publish(publisher2, msg2);

    REQUIRE(subscriber.is_overrun());
    REQUIRE(subscriber.almost_overrun());
  }
}

using ZeroInputsPolicyFixture = CogInputsFixture<>;

TEST_CASE_METHOD(ZeroInputsPolicyFixture, "zero subscribers", "[cog_inputs]")
{
  REQUIRE(subscriber.validate());

  // No active conditions before receiving any messages

  auto conds = make_active_conds();
  auto inputs = subscriber.make_dial_inputs<ConditionsType>(conds, fake_publish_time);
  REQUIRE(inputs);
  REQUIRE(0 == std::tuple_size<std::decay_t<decltype(*inputs)>>());
  REQUIRE_FALSE(subscriber.is_overrun());

  std::ignore = subscriber.commit(*inputs);
  REQUIRE_FALSE(subscriber.is_overrun());
}

} // namespace
} // namespace clockwork
