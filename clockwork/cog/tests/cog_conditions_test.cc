// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/cog_conditions.hh"
#include "clockwork/cog/input_condition.hh"
#include "clockwork/common/process_description.hh"
#include "clockwork/dial/cond_messages_present.hh"
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/in_memory_channel.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "jewels/container/compare.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>
#include <gsl/util>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <limits>
#include <memory_resource>
#include <string_view>
#include <tuple>
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
  void notify(jewels::time::SyncTime /*current_time8*/) {}
};

template <typename... Types, typename... Args>
auto make_tuple_repeat(Args&... args)
{
  // NOLINTNEXTLINE(bugprone-use-after-move) TODO(DX-1794): Fix this
  return std::tuple<Types...>(Types{std::forward<Args...>(args...)}...);
}

template <typename ConditionPolicy>
struct TestViewPolicy
{
  static constexpr auto endpoint_id = ConditionPolicy::endpoint_id;
  using ConditionType = typename InputCondition<ConditionPolicy>::ConditionType;
  static ConditionType make_cond(bool active, uint32_t num_msgs)
  {
    return ConditionType(active, num_msgs);
  }
};

template <typename... Policies>
struct CogConditionsFixture // NOLINT(clang-analyzer-optin.performance.Padding). Test code performance is not a concern.
{
  static constexpr auto policy_count = sizeof...(Policies);
  using ConditionsType = CogConditions<Policies...>;
  using PinionDifferenceType = typename ConditionsType::PinionDifferenceType;
  using ChannelsTuple = std::tuple<InMemoryChannel<typename Policies::MsgType, Policies::channel_size>...>;
  using PublishersArray = std::array<pinion::PublisherHandle, policy_count>;
  using SubscribersArray = std::array<pinion::SubscriberHandle, policy_count>;

  CogConditionsFixture()
    : resource(std::pmr::new_delete_resource()),
      channels(make_tuple_repeat<InMemoryChannel<typename Policies::MsgType, Policies::channel_size>...>(resource)),
      publishers(
        std::apply([](auto&... channel) -> PublishersArray { return {channel.make_publisher(1)...}; }, channels)),
      subscribers(
        std::apply([](auto&... channel) -> SubscribersArray { return {channel.make_subscriber()...}; }, channels)),
      conditions(resource)
  {
  }

  jewels::memory::MemoryResource resource;
  TestCog cog;
  ChannelsTuple channels;
  PublishersArray publishers;
  SubscribersArray subscribers;
  ConditionsType conditions;

  /// Publish the message and return a pointer to the published message.
  template <typename MsgType>
  void publish(pinion::PublisherHandle& handle, const MsgType& msg)
  {
    auto slot = handle.reserve().value();
    auto publishable = pinion::Publishable<MsgType>::try_make(jewels::memory::make_non_null_from_ref(slot)).value();
    publishable.message() = msg;
    REQUIRE(slot.commit(fake_publish_time));
  }

  template <std::size_t i>
  [[nodiscard]] auto make_iterator(size_t index) const
  {
    return std::next(std::get<i>(subscribers).available().begin(), static_cast<ssize_t>(index));
  }
};

struct NewInputPolicy
{
  using MsgType = TestMsg1;
  static constexpr auto endpoint_id =
    jewels::Uuid<common::EndpointClassId>::from_string("b6e2b628-62ba-4c73-b07e-b2ce77a742b4").value();
  static constexpr std::string_view name = "NewInputPolicy";
  static constexpr auto bounds_min = 1U;
  static constexpr auto bounds_max = 1U;
  static constexpr auto condition_type = InputConditionType::new_message;
  // Testing only
  static constexpr auto channel_size = 3U;
};

struct AnyInputPolicy
{
  using MsgType = TestMsg2;
  static constexpr auto endpoint_id =
    jewels::Uuid<common::EndpointClassId>::from_string("789e340c-556f-4cf0-a3b9-73632ba75a7c").value();
  static constexpr std::string_view name = "AnyInputPolicy";
  static constexpr auto bounds_min = 1U;
  static constexpr auto bounds_max = 1U;
  static constexpr auto condition_type = InputConditionType::any_message;
  // Testing only
  static constexpr auto channel_size = 3U;
};

using ConditionPolicyFixture = CogConditionsFixture<NewInputPolicy, AnyInputPolicy>;

TEST_CASE_METHOD(ConditionPolicyFixture, "basic operation", "[cog_conditions]")
{
  // Make publishers

  auto& publisher1 = publishers.at(0);
  auto& publisher2 = publishers.at(1);

  // Set subcriber handles

  SECTION("set_handle unknown id")
  {
    constexpr auto unknown_id =
      jewels::Uuid<common::EndpointClassId>::from_string("b5e2c9a9-e351-4877-b5cc-77e76a7ebe63").value();
    REQUIRE_FALSE(conditions.set_handle(unknown_id, std::get<0>(channels).make_subscriber()));
  }

  REQUIRE(conditions.set_handle(NewInputPolicy::endpoint_id, std::get<0>(channels).make_subscriber()));
  REQUIRE(conditions.set_handle(AnyInputPolicy::endpoint_id, std::get<1>(channels).make_subscriber()));
  REQUIRE(conditions.validate());

  // No active conditions before receiving any messages

  auto conds = conditions.make_conditions();
  REQUIRE_FALSE(std::get<0>(conds));
  REQUIRE_FALSE(std::get<1>(conds));

  // Publish message to channel 1

  auto msg1 = TestMsg1{.value = 1};
  publish(publisher1, msg1);

  // Only condition one should be active

  conds = conditions.make_conditions();
  REQUIRE(std::get<0>(conds));
  REQUIRE(1 == std::get<0>(conds).get_num_messages());
  REQUIRE_FALSE(std::get<1>(conds));

  // Publish message to channel 2

  auto msg2 = TestMsg2{.value = 2};
  publish(publisher2, msg2);

  // All the conditions should be active

  conds = conditions.make_conditions();
  REQUIRE(std::get<0>(conds));
  REQUIRE(1 == std::get<0>(conds).get_num_messages());
  REQUIRE(std::get<1>(conds));
  REQUIRE(1 == std::get<1>(conds).get_num_messages());

  SECTION("do not advance last consumed until update is called")
  {
    conds = conditions.make_conditions();
    REQUIRE(std::get<0>(conds));
    REQUIRE(std::get<1>(conds));
  }

  SECTION("update last consumed for new condition results in false condition")
  {
    auto& cond0 = std::get<0>(conds);
    conditions.commit(NewInputPolicy::endpoint_id, std::next(make_iterator<0>(2)));
    conds = conditions.make_conditions();

    cond0 = std::get<0>(conds);
    REQUIRE_FALSE(std::get<0>(conds));
    REQUIRE(std::get<1>(conds));
  }

  SECTION("update last consumed array for new condition results in false condition")
  {
    auto records_arr = std::array{std::make_tuple(NewInputPolicy::endpoint_id, std::next(make_iterator<0>(1)))};

    auto& cond0 = std::get<0>(conds);
    conditions.commit(records_arr);
    conds = conditions.make_conditions();

    cond0 = std::get<0>(conds);
    REQUIRE_FALSE(std::get<0>(conds));
    REQUIRE(std::get<1>(conds));
  }

  SECTION("update last consumed for any condition results in true condition")
  {
    conditions.commit(AnyInputPolicy::endpoint_id, std::next(make_iterator<1>(1)));
    conds = conditions.make_conditions();

    REQUIRE(std::get<0>(conds));
    REQUIRE(std::get<1>(conds));
  }
}

using ZeroConditionsPolicyFixture = CogConditionsFixture<>;

TEST_CASE_METHOD(ZeroConditionsPolicyFixture, "zero conditionss", "[cog_conditions]")
{
  REQUIRE(conditions.validate());

  // No active conditions before receiving any messages

  [[maybe_unused]] auto conds = conditions.make_conditions();
  REQUIRE(0 == std::tuple_size<decltype(conds)>());
}

TEST_CASE_METHOD(ZeroConditionsPolicyFixture, "get_max_new_msgs", "[cog_conditions]")
{
  auto conds = ConditionsType::ConditionsTuple();
  auto actual = ConditionsType::get_max_new_msgs<TestViewPolicy<NewInputPolicy>>(conds);
  REQUIRE(std::numeric_limits<PinionDifferenceType>::max() == actual);
}

struct New1Id1InputPolicy
{
  using MsgType = TestMsg1;
  static constexpr auto endpoint_id =
    jewels::Uuid<common::EndpointClassId>::from_string("b6e2b628-62ba-4c73-b07e-b2ce77a742b4").value();
  [[maybe_unused]] static constexpr std::string_view name = "NewInputPolicy";
  static constexpr auto bounds_min = 1U;
  static constexpr auto bounds_max = 1U;
  static constexpr auto condition_type = InputConditionType::new_message;
  // Testing only
  static constexpr auto channel_size = 3U;
};

struct New3Id1InputPolicy
{
  using MsgType = TestMsg1;
  static constexpr auto endpoint_id =
    jewels::Uuid<common::EndpointClassId>::from_string("b6e2b628-62ba-4c73-b07e-b2ce77a742b4").value();
  [[maybe_unused]] static constexpr std::string_view name = "NewInputPolicy";
  static constexpr auto bounds_min = 1U;
  static constexpr auto bounds_max = 3U;
  static constexpr auto condition_type = InputConditionType::new_message;
  // Testing only
  static constexpr auto channel_size = 3U;
};

struct Any2Id2InputPolicy
{
  using MsgType = TestMsg2;
  static constexpr auto endpoint_id =
    jewels::Uuid<common::EndpointClassId>::from_string("789e340c-556f-4cf0-a3b9-73632ba75a7c").value();
  [[maybe_unused]] static constexpr std::string_view name = "AnyInputPolicy";
  static constexpr auto bounds_min = 1U;
  static constexpr auto bounds_max = 1U;
  static constexpr auto condition_type = InputConditionType::any_message;
  // Testing only
  static constexpr auto channel_size = 3U;
};

struct Any2Id3InputPolicy
{
  using MsgType = TestMsg2;
  static constexpr auto endpoint_id =
    jewels::Uuid<common::EndpointClassId>::from_string("cc18c7c9-8279-4692-99cd-b04e602a89d9").value();
  [[maybe_unused]] static constexpr std::string_view name = "AnyInputPolicy";
  static constexpr auto bounds_min = 1U;
  static constexpr auto bounds_max = 2U;
  static constexpr auto condition_type = InputConditionType::any_message;
};

using GetNewMsgsPolicyFixture = CogConditionsFixture<New1Id1InputPolicy, New3Id1InputPolicy, Any2Id2InputPolicy>;

TEST_CASE_METHOD(GetNewMsgsPolicyFixture, "get_max_new_msgs", "[cog_conditions]")
{
  using New1Id1CondView = TestViewPolicy<New1Id1InputPolicy>;
  using New3Id1CondView = TestViewPolicy<New3Id1InputPolicy>;
  using Any2Id2CondView = TestViewPolicy<Any2Id2InputPolicy>;
  using Any2Id3CondView = TestViewPolicy<Any2Id3InputPolicy>;

  SECTION("inactive conds")
  {
    auto conds = ConditionsType::ConditionsTuple(
      New1Id1CondView::make_cond(false, 1), New3Id1CondView::make_cond(false, 0), Any2Id2CondView::make_cond(false, 0));
    REQUIRE(0 == ConditionsType::get_max_new_msgs<New1Id1CondView>(conds));
    REQUIRE(0 == ConditionsType::get_max_new_msgs<New3Id1CondView>(conds));
    REQUIRE(0 == ConditionsType::get_max_new_msgs<Any2Id2CondView>(conds));
  }

  SECTION("first cond active")
  {
    auto conds = ConditionsType::ConditionsTuple(
      New1Id1CondView::make_cond(true, 1), New3Id1CondView::make_cond(false, 0), Any2Id2CondView::make_cond(false, 0));
    REQUIRE(1 == ConditionsType::get_max_new_msgs<New1Id1CondView>(conds));
    REQUIRE(1 == ConditionsType::get_max_new_msgs<New3Id1CondView>(conds));
    REQUIRE(0 == ConditionsType::get_max_new_msgs<Any2Id2CondView>(conds));
  }

  SECTION("second cond active")
  {
    auto conds = ConditionsType::ConditionsTuple(
      New1Id1CondView::make_cond(false, 0), New3Id1CondView::make_cond(true, 1), Any2Id2CondView::make_cond(false, 0));
    REQUIRE(1 == ConditionsType::get_max_new_msgs<New1Id1CondView>(conds));
    REQUIRE(1 == ConditionsType::get_max_new_msgs<New3Id1CondView>(conds));
    REQUIRE(0 == ConditionsType::get_max_new_msgs<Any2Id2CondView>(conds));
  }

  SECTION("third cond active")
  {
    auto conds = ConditionsType::ConditionsTuple(
      New1Id1CondView::make_cond(false, 0), New3Id1CondView::make_cond(false, 0), Any2Id2CondView::make_cond(true, 1));
    REQUIRE(0 == ConditionsType::get_max_new_msgs<New1Id1CondView>(conds));
    REQUIRE(0 == ConditionsType::get_max_new_msgs<New3Id1CondView>(conds));
    REQUIRE(1 == ConditionsType::get_max_new_msgs<Any2Id2CondView>(conds));
  }

  SECTION("all cond active")
  {
    auto conds = ConditionsType::ConditionsTuple(
      New1Id1CondView::make_cond(true, 1), New3Id1CondView::make_cond(true, 1), Any2Id2CondView::make_cond(true, 1));
    REQUIRE(1 == ConditionsType::get_max_new_msgs<New1Id1CondView>(conds));
    REQUIRE(1 == ConditionsType::get_max_new_msgs<New3Id1CondView>(conds));
    REQUIRE(1 == ConditionsType::get_max_new_msgs<Any2Id2CondView>(conds));
  }

  SECTION("first (num_msgs=1) and second (num_msgs=1)")
  {
    auto conds = ConditionsType::ConditionsTuple(
      New1Id1CondView::make_cond(true, 1), New3Id1CondView::make_cond(true, 1), Any2Id2CondView::make_cond(false, 0));
    REQUIRE(1 == ConditionsType::get_max_new_msgs<New1Id1CondView>(conds));
    REQUIRE(1 == ConditionsType::get_max_new_msgs<New3Id1CondView>(conds));
    REQUIRE(0 == ConditionsType::get_max_new_msgs<Any2Id2CondView>(conds));
  }

  SECTION("first (num_msgs=1) and second (num_msgs=3)")
  {
    auto conds = ConditionsType::ConditionsTuple(
      New1Id1CondView::make_cond(true, 1), New3Id1CondView::make_cond(true, 3), Any2Id2CondView::make_cond(false, 0));
    REQUIRE(1 == ConditionsType::get_max_new_msgs<New1Id1CondView>(conds));
    REQUIRE(1 == ConditionsType::get_max_new_msgs<New3Id1CondView>(conds));
    REQUIRE(0 == ConditionsType::get_max_new_msgs<Any2Id2CondView>(conds));
  }

  SECTION("first (num_msgs=1) and second (num_msgs=3) and third (num_msgs=2)")
  {
    auto conds = ConditionsType::ConditionsTuple(
      New1Id1CondView::make_cond(true, 1), New3Id1CondView::make_cond(true, 3), Any2Id2CondView::make_cond(true, 2));
    REQUIRE(1 == ConditionsType::get_max_new_msgs<New1Id1CondView>(conds));
    REQUIRE(1 == ConditionsType::get_max_new_msgs<New3Id1CondView>(conds));
    REQUIRE(2 == ConditionsType::get_max_new_msgs<Any2Id2CondView>(conds));
  }

  SECTION("view with no conds returns max")
  {
    auto conds = ConditionsType::ConditionsTuple(
      New1Id1CondView::make_cond(true, 1), New3Id1CondView::make_cond(true, 3), Any2Id2CondView::make_cond(true, 2));
    REQUIRE(
      std::numeric_limits<PinionDifferenceType>::max() == ConditionsType::get_max_new_msgs<Any2Id3CondView>(conds));
  }
}

} // namespace
} // namespace clockwork
