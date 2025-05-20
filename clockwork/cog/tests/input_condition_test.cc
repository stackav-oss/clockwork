// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/input_condition.hh"
#include "clockwork/common/process_description.hh"
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/in_memory_channel.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>
#include <gsl/util>

#include <chrono>
#include <cstdint>
#include <iterator>
#include <memory_resource>
#include <sys/types.h>

namespace clockwork
{
namespace
{

inline constexpr jewels::time::SyncTime fake_publish_time{std::chrono::nanoseconds{12345}};

struct TestMsg
{
  int32_t value;
  bool operator==(const TestMsg&) const = default;
};

template <typename Policy>
struct InputConditionFixture // NOLINT(clang-analyzer-optin.performance.Padding)
{
  using MsgType = typename Policy::MsgType;

  InputConditionFixture()
    : resource(std::pmr::new_delete_resource()),
      channel(resource),
      publisher_handle(channel.make_publisher(1)),
      subscriber_handle(channel.make_subscriber()),
      subscriber(subscriber_handle)
  {
  }

  jewels::memory::MemoryResource resource;
  InMemoryChannel<MsgType, Policy::max_view_size> channel;
  pinion::PublisherHandle publisher_handle;
  pinion::SubscriberHandle subscriber_handle;
  InputCondition<Policy> subscriber;

  /// Publish the message
  void publish(const MsgType& msg)
  {
    auto slot = publisher_handle.reserve().value();
    auto publishable = pinion::Publishable<MsgType>::try_make(jewels::memory::make_non_null_from_ref(slot)).value();
    publishable.message() = msg;
    REQUIRE(slot.commit(fake_publish_time));
  }

  [[nodiscard]] auto make_iterator(size_t index) const
  {
    return std::next(subscriber_handle.available().begin(), static_cast<ssize_t>(index));
  }
};

struct NewMin1Max1Policy
{
  using MsgType = TestMsg;
  static constexpr auto endpoint_id =
    jewels::Uuid<common::EndpointClassId>::from_string("27e38987-2e30-497f-9349-bf8d98931470").value();
  static constexpr auto max_view_size = 3U;
  static constexpr auto bounds_min = 1U;
  static constexpr auto bounds_max = 1U;
  static constexpr auto condition_type = InputConditionType::new_message;
};

using NewMin1Max1Fixture = InputConditionFixture<NewMin1Max1Policy>;

TEST_CASE_METHOD(NewMin1Max1Fixture, "no messages", "[input=new bounds_min=1 bounds_max=1 copy=false]")
{
  // The condition should be false before any messages are published.

  REQUIRE_FALSE(subscriber.make_condition());

  // Updating the last consumed shoudl be a no-op.

  subscriber.commit(make_iterator(0));

  // The conditions should still false since no messages are published.

  REQUIRE_FALSE(subscriber.make_condition());
}

TEST_CASE_METHOD(NewMin1Max1Fixture, "publish one message", "[input=new bounds_min=1 bounds_max=1 copy=false]")
{
  // Publish message.

  auto msg = TestMsg{.value = 0};
  publish(msg);

  // The condition should pass with one available message.

  auto cond = subscriber.make_condition();
  REQUIRE(cond);
  REQUIRE(1 == cond.get_num_messages());

  SECTION("do not advance last consumed until update is called")
  {
    cond = subscriber.make_condition();
    REQUIRE(cond);
    REQUIRE(1 == cond.get_num_messages());
  }

  SECTION("no messages available after update last consumed is called")
  {
    subscriber.commit(make_iterator(1));

    REQUIRE_FALSE(subscriber.make_condition());
  }
}

TEST_CASE_METHOD(NewMin1Max1Fixture, "publish three messages", "[input=new bounds_min=1 bounds_max=1 copy=false]")
{
  // Publish message.

  auto msg0 = TestMsg{.value = 0};
  auto msg1 = TestMsg{.value = 1};
  auto msg2 = TestMsg{.value = 2};

  publish(msg0);
  publish(msg1);
  publish(msg2);

  // Process all the expected messages.

  for (size_t i = 0; i < 3; ++i)
  {
    auto cond = subscriber.make_condition();
    REQUIRE(cond);
    REQUIRE(1 == cond.get_num_messages());

    subscriber.commit(std::next(make_iterator(i)));
  }

  REQUIRE_FALSE(subscriber.make_condition());
}

TEST_CASE_METHOD(NewMin1Max1Fixture, "subscriber falling behind", "[input=new bounds_min=1 bounds_max=1 copy=false]")
{
  // Publish message.

  auto msg0 = TestMsg{.value = 0};
  auto msg1 = TestMsg{.value = 1};
  auto msg2 = TestMsg{.value = 2};
  auto msg3 = TestMsg{.value = 3};

  publish(msg0);
  publish(msg1);
  publish(msg2);
  publish(msg3);

  // Process all the expected messages (queue size is 3 so msg0 should not be available).

  for (size_t i = 0; i < 3; ++i)
  {
    auto cond = subscriber.make_condition();
    REQUIRE(cond);
    REQUIRE(1 == cond.get_num_messages());

    subscriber.commit(std::next(make_iterator(i)));
  }

  REQUIRE_FALSE(subscriber.make_condition());
}

TEST_CASE_METHOD(
  NewMin1Max1Fixture,
  "subscriber falling behind with valid last viewed",
  "[input=new bounds_min=1 bounds_max=1 copy=false]")
{
  // Publish message.

  auto msg0 = TestMsg{.value = 0};
  auto msg1 = TestMsg{.value = 1};
  auto msg2 = TestMsg{.value = 2};
  auto msg3 = TestMsg{.value = 3};
  auto msg4 = TestMsg{.value = 4};
  auto msg5 = TestMsg{.value = 5};
  auto msg6 = TestMsg{.value = 6};
  auto msg7 = TestMsg{.value = 7};

  // Update the last viewed

  publish(msg0);

  {
    auto cond = subscriber.make_condition();
    REQUIRE(cond);
    REQUIRE(1 == cond.get_num_messages());
    subscriber.commit(std::next(make_iterator(0)));
  }

  // Publish enough messages to roll the queue over.

  publish(msg1);
  publish(msg2);
  publish(msg3);
  publish(msg4);
  publish(msg5);
  publish(msg6);
  publish(msg7);

  // Process all the expected messages (queue size is 3 so msg0 should not be available).

  for (size_t i = 0; i < 3; ++i)
  {
    auto cond = subscriber.make_condition();
    REQUIRE(cond);
    REQUIRE(1 == cond.get_num_messages());

    subscriber.commit(std::next(make_iterator(i)));
  }

  REQUIRE_FALSE(subscriber.make_condition());
}

struct NewMin2Max5Policy
{
  using MsgType = TestMsg;
  static constexpr auto endpoint_id =
    jewels::Uuid<common::EndpointClassId>::from_string("788ae793-68eb-422b-bf41-6750107dccb2").value();
  static constexpr auto max_view_size = 10U;
  static constexpr auto bounds_min = 2U;
  static constexpr auto bounds_max = 5U;
  static constexpr auto condition_type = InputConditionType::new_message;
};

using NewMin2Max5Fixture = InputConditionFixture<NewMin2Max5Policy>;

TEST_CASE_METHOD(NewMin2Max5Fixture, "min max bounds respected", "[input=new bounds_min=2 bounds_max=5 copy=false]")
{
  // Publish message.

  auto msg0 = TestMsg{.value = 0};
  auto msg1 = TestMsg{.value = 1};
  auto msg2 = TestMsg{.value = 2};
  auto msg3 = TestMsg{.value = 3};
  auto msg4 = TestMsg{.value = 4};
  auto msg5 = TestMsg{.value = 5};
  auto msg6 = TestMsg{.value = 6};
  auto msg7 = TestMsg{.value = 7};

  publish(msg0);

  // Condition should be false (1 < 2 min)

  {
    auto cond = subscriber.make_condition();
    REQUIRE_FALSE(cond);
    REQUIRE(1 == cond.get_num_messages());
  }

  // Publish another message, condition should be true now.

  publish(msg1);

  {
    auto cond = subscriber.make_condition();
    REQUIRE(cond);
    REQUIRE(2 == cond.get_num_messages());
  }

  // Publish another message, condition should still be true with (num == 3), since we havent consumed inputs yet.

  publish(msg2);

  size_t last_consumed = 3;

  {
    auto cond = subscriber.make_condition();
    REQUIRE(cond);
    REQUIRE(3 == cond.get_num_messages());

    subscriber.commit(make_iterator(last_consumed));
  }

  // Should have consumed all messages.

  REQUIRE_FALSE(subscriber.make_condition());

  // Publish over the bounds, consuming inputs should process the first 5.

  publish(msg0);
  publish(msg1);
  publish(msg2);
  publish(msg3);
  publish(msg4);
  publish(msg5);
  publish(msg6);
  publish(msg7);

  {
    auto cond = subscriber.make_condition();
    REQUIRE(cond);
    REQUIRE(5 == cond.get_num_messages());

    last_consumed += 4;
    subscriber.commit(std::next(make_iterator(last_consumed)));
  }

  // Making the inputs again should process the remaining three.

  {
    auto cond = subscriber.make_condition();
    REQUIRE(cond);
    CHECK(2 == cond.get_num_messages());

    last_consumed += 1;
    subscriber.commit(std::next(make_iterator(last_consumed)));
  }

  REQUIRE_FALSE(subscriber.make_condition());
}

struct AnyMin1Max1Policy
{
  using MsgType = TestMsg;
  static constexpr auto endpoint_id =
    jewels::Uuid<common::EndpointClassId>::from_string("2f6fde88-e445-4f5c-aa52-90e5ecf513c9").value();
  static constexpr auto max_view_size = 3U;
  static constexpr auto bounds_min = 1U;
  static constexpr auto bounds_max = 1U;
  static constexpr auto condition_type = InputConditionType::any_message;
};

using AnyMin1Max1Fixture = InputConditionFixture<AnyMin1Max1Policy>;

TEST_CASE_METHOD(AnyMin1Max1Fixture, "no messages", "[input=any bounds_min=1 bounds_max=1 copy=false]")
{
  // The condition should be false before any messages are published.

  REQUIRE_FALSE(subscriber.make_condition());

  // Updating the last consumed shoudl be a no-op.

  subscriber.commit({});

  // The conditions should still false since no messages are published.

  REQUIRE_FALSE(subscriber.make_condition());
}

TEST_CASE_METHOD(AnyMin1Max1Fixture, "publish one message", "[input=any bounds_min=1 bounds_max=1 copy=false]")
{
  // Publish message.

  auto msg = TestMsg{.value = 0};
  publish(msg);

  // The condition should pass with one available message.

  auto cond = subscriber.make_condition();
  REQUIRE(cond);
  REQUIRE(1 == cond.get_num_messages());

  SECTION("return message before advancing")
  {
    cond = subscriber.make_condition();
    REQUIRE(cond);
    REQUIRE(1 == cond.get_num_messages());
  }

  // Even after advancing any message policy should still return the exiting message.

  SECTION("return message after advancing")
  {
    subscriber.commit({});

    cond = subscriber.make_condition();
    REQUIRE(cond);
    REQUIRE(1 == cond.get_num_messages());
  }
}

TEST_CASE_METHOD(AnyMin1Max1Fixture, "publish three messages", "[input=any bounds_min=1 bounds_max=1 copy=false]")
{
  // Publish message.

  auto msg0 = TestMsg{.value = 0};
  auto msg1 = TestMsg{.value = 1};
  auto msg2 = TestMsg{.value = 2};

  publish(msg0);
  publish(msg1);
  publish(msg2);

  // Process all the expected messages, should only be the last (latest) message published.

  {
    auto cond = subscriber.make_condition();
    REQUIRE(cond);
    REQUIRE(1 == cond.get_num_messages());

    subscriber.commit({});
  }

  // Should continue to return the same input.

  {
    auto cond = subscriber.make_condition();
    REQUIRE(cond);
    REQUIRE(1 == cond.get_num_messages());
  }
}

} // namespace
} // namespace clockwork
