// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/input_view.hh"
#include "clockwork/common/process_description.hh"
#include "clockwork/dial/msg_input.hh"
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/in_memory_channel.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "jewels/container/circular_buffer.hh"
#include "jewels/memory/memory_resource.hh"
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
#include <iterator>
#include <limits>
#include <memory_resource>
#include <ranges>
#include <tuple>

namespace clockwork
{
namespace
{

/// Fixed timestamp for publish timestamp.
inline constexpr auto fake_publish_time{jewels::time::SyncTime{std::chrono::nanoseconds{12345}}};

struct TestMsg
{
  int32_t value;
  bool operator==(const TestMsg&) const = default;
};

template <typename PolicyType>
struct InputViewFixture // NOLINT(clang-analyzer-optin.performance.Padding) Test code so performance is not a concern.
{
  using Policy = PolicyType;
  using MsgType = typename Policy::MsgType;
  using PinionDifferenceType = typename InputView<Policy>::PinionDifferenceType;
  static constexpr auto new_msgs_max_limit = std::numeric_limits<PinionDifferenceType>::max();

  InputViewFixture()
    : resource(std::pmr::new_delete_resource()),
      channel(resource),
      publisher_handle(channel.make_publisher(1)),
      subscriber_handle(channel.make_subscriber()),
      subscriber(subscriber_handle)
  {
  }

  jewels::memory::MemoryResource resource;
  InMemoryChannel<MsgType, Policy::channel_size> channel;
  pinion::PublisherHandle publisher_handle;
  pinion::SubscriberHandle subscriber_handle;
  InputView<Policy> subscriber;

  /// Publish the message and return a pointer to the published message.
  const MsgType* publish_and_return_ptr(const MsgType& msg)
  {
    auto slot = publisher_handle.reserve().value();
    auto publishable = pinion::Publishable<MsgType>::try_make(jewels::memory::make_non_null_from_ref(slot)).value();
    publishable.message() = msg;
    REQUIRE(slot.commit(fake_publish_time));
    return &publishable.message();
  }

  /// Publish the message
  void publish(const MsgType& msg)
  {
    auto slot = publisher_handle.reserve().value();
    auto publishable = pinion::Publishable<MsgType>::try_make(jewels::memory::make_non_null_from_ref(slot)).value();
    publishable.message() = msg;
    REQUIRE(slot.commit(fake_publish_time));
  }
};

struct NoCopyPolicy
{
  using MsgType = TestMsg;
  static constexpr auto name = "NoCopyPolicy";
  static constexpr auto endpoint_id =
    jewels::Uuid<common::EndpointClassId>::from_string("27e38987-2e30-497f-9349-bf8d98931470").value();
  static constexpr auto max_view_size = 3U;
  static constexpr auto copy_inputs = false;
  static constexpr auto manual_cursor = false;
  /// Testing only
  static constexpr auto channel_size = 3U;
};

using NoCopyFixture = InputViewFixture<NoCopyPolicy>;

TEST_CASE_METHOD(NoCopyFixture, "no messages", "[max_view_size=3, channel_size=3, copy=false, manual=false]")
{
  REQUIRE(subscriber.validate());

  // The view should be empty before any messages are published

  auto input = subscriber.make_dial_input(new_msgs_max_limit);
  REQUIRE(input);
  REQUIRE(input->get_cursor() == input->end());

  // Updating the last consumed shoudl be a no-op.

  auto last_consumed = subscriber.commit(*input);
  REQUIRE(NoCopyFixture::Policy::endpoint_id == std::get<0>(last_consumed));
  REQUIRE(pinion::BufferIterator{} == std::get<1>(last_consumed));

  // The view should be empty since no messages are published.

  input = subscriber.make_dial_input(new_msgs_max_limit);
  REQUIRE(input);
  REQUIRE(input->get_cursor() == input->end());
}

TEST_CASE_METHOD(NoCopyFixture, "publish one message", "[max_view_size=3, channel_size=3, copy=false, manual=false]")
{
  REQUIRE(subscriber.validate());

  // Publish message.

  auto msg = TestMsg{.value = 0};
  const auto* msg_ptr = publish_and_return_ptr(msg);

  // The inputs view should only contain the message.

  auto input = subscriber.make_dial_input(new_msgs_max_limit);
  REQUIRE(input);
  auto cursor = input->get_cursor();
  REQUIRE(cursor != input->end());
  REQUIRE(msg == *cursor);
  REQUIRE(input->end() == std::next(cursor));

  // Verify the input message is pointing to the original publish location (i.e. not copied).

  REQUIRE(msg_ptr == &(*cursor));
}

TEST_CASE_METHOD(
  NoCopyFixture, "make inputs with zero new messages", "[max_view_size=1, channel_size=3, copy=false, manual=false]")
{
  REQUIRE(subscriber.validate());

  // Publish message.

  auto msg = TestMsg{.value = 0};
  publish(msg);
  publish(msg);
  publish(msg);

  // The inputs view should only contain the message.

  auto input = subscriber.make_dial_input(new_msgs_max_limit);
  REQUIRE(input);
  auto cursor = input->get_cursor();
  REQUIRE(cursor == input->get_view().begin());
  REQUIRE(cursor != input->end());
  REQUIRE(msg == *cursor);
  REQUIRE(3 == input->get_view().size());
  REQUIRE(3 == input->get_new_msgs_view().size());

  // Commit the view.

  std::ignore = subscriber.commit(*input);

  // Make inputs with no new messages. Cursor view should still be one but cursor points to the end.

  input = subscriber.make_dial_input(0);
  REQUIRE(input);
  REQUIRE(3 == input->get_view().size());
  REQUIRE(input->get_new_msgs_view().empty());
  REQUIRE(input->get_cursor() == input->get_view().end());
  REQUIRE(input->get_cursor() == input->end());
}

TEST_CASE_METHOD(NoCopyFixture, "first new", "[max_view_size=3, channel_size=3, copy=false, manual=false]")
{
  REQUIRE(subscriber.validate());

  // Publish message.

  auto msg0 = TestMsg{.value = 0};
  auto msg1 = TestMsg{.value = 1};
  auto msg2 = TestMsg{.value = 2};
  auto msg3 = TestMsg{.value = 3};

  auto input = subscriber.make_dial_input(new_msgs_max_limit);
  REQUIRE(input);
  REQUIRE(input->get_cursor_view().empty());

  // Publish one message

  publish(msg0);

  input = subscriber.make_dial_input(new_msgs_max_limit);
  REQUIRE(input);
  REQUIRE(1 == input->get_cursor_view().size());
  REQUIRE(input->get_cursor() != input->end());
  REQUIRE(input->get_first_new() == input->get_cursor());
  REQUIRE(msg0 == *input->get_cursor());
  REQUIRE(msg0 == *input->get_first_new());

  // Update last consumed, first new is end

  std::ignore = subscriber.commit(*input);

  input = subscriber.make_dial_input(new_msgs_max_limit);
  REQUIRE(input);
  REQUIRE(input->get_cursor_view().empty());
  REQUIRE(input->get_cursor() == input->end());
  REQUIRE(input->get_first_new() == input->end());

  // Publish two messages, first new is msg1

  publish(msg1);
  publish(msg2);

  input = subscriber.make_dial_input(new_msgs_max_limit);
  REQUIRE(input);
  REQUIRE(2 == input->get_cursor_view().size());
  REQUIRE(input->get_cursor() != input->end());
  REQUIRE(input->get_first_new() != input->end());
  REQUIRE(msg1 == *input->get_cursor());
  REQUIRE(msg1 == *input->get_first_new());

  // Publish last message, first new is still msg1

  publish(msg3);

  input = subscriber.make_dial_input(new_msgs_max_limit);
  REQUIRE(input);
  REQUIRE(3 == input->get_cursor_view().size());
  REQUIRE(input->get_cursor() != input->end());
  REQUIRE(input->get_first_new() == input->get_cursor());
  REQUIRE(input->get_first_new() != input->end());
  REQUIRE(msg1 == *input->get_cursor());
  REQUIRE(msg1 == *input->get_first_new());

  // Update last consumed, first new is end

  std::ignore = subscriber.commit(*input);

  input = subscriber.make_dial_input(new_msgs_max_limit);
  REQUIRE(input);
  REQUIRE(input->get_cursor_view().empty());
  REQUIRE(input->get_cursor() == input->end());
  REQUIRE(input->get_first_new() == input->end());
}

TEST_CASE_METHOD(NoCopyFixture, "view max messages", "[max_view_size=3, channel_size=3, copy=false, manual=false]")
{
  REQUIRE(subscriber.validate());

  // Publish message.

  auto msg0 = TestMsg{.value = 0};
  auto msg1 = TestMsg{.value = 1};
  auto msg2 = TestMsg{.value = 2};
  auto msg3 = TestMsg{.value = 3};

  const auto* msg0_ptr = publish_and_return_ptr(msg0);
  REQUIRE(msg0_ptr != nullptr);

  const auto* msg1_ptr = publish_and_return_ptr(msg1);
  const auto* msg2_ptr = publish_and_return_ptr(msg2);
  const auto* msg3_ptr = publish_and_return_ptr(msg3);

  // The inputs view should only contain the message.

  auto input = subscriber.make_dial_input(new_msgs_max_limit);
  REQUIRE(input);

  auto cursor = input->get_cursor();
  REQUIRE(cursor != input->end());
  REQUIRE(msg1 == *cursor);
  REQUIRE(msg1_ptr == &(*cursor));
  REQUIRE(cursor == input->get_first_new());

  cursor = std::next(cursor);
  REQUIRE(cursor != input->end());
  REQUIRE(msg2 == *cursor);
  REQUIRE(msg2_ptr == &(*cursor));

  cursor = std::next(cursor);
  REQUIRE(cursor != input->end());
  REQUIRE(msg3 == *cursor);
  REQUIRE(msg3_ptr == &(*cursor));

  cursor = std::next(cursor);
  REQUIRE(cursor == input->end());

  // Update last input sets first new to end

  std::ignore = subscriber.commit(*input);
  input = subscriber.make_dial_input(new_msgs_max_limit);
  REQUIRE(input);
  REQUIRE(input->end() == input->get_first_new());
}

TEST_CASE_METHOD(NoCopyFixture, "overrun", "[max_view_size=3, channel_size=3, copy=false, manual=false]")
{
  REQUIRE(subscriber.validate());

  // Publish message.

  auto msg0 = TestMsg{.value = 0};
  auto msg1 = TestMsg{.value = 1};
  auto msg2 = TestMsg{.value = 2};
  auto msg3 = TestMsg{.value = 3};

  publish(msg0);

  auto input = subscriber.make_dial_input(new_msgs_max_limit);
  REQUIRE(input);

  REQUIRE_FALSE(subscriber.is_overrun());

  publish(msg1);

  REQUIRE_FALSE(subscriber.is_overrun());

  publish(msg2);

  REQUIRE_FALSE(subscriber.is_overrun());

  publish(msg3);

  REQUIRE(subscriber.is_overrun());

  SECTION("clear after commit()")
  {
    std::ignore = subscriber.commit(*input);
    REQUIRE_FALSE(subscriber.is_overrun());
  }

  SECTION("clear after make_dial_input()")
  {
    input = subscriber.make_dial_input(new_msgs_max_limit);
    REQUIRE(input);

    REQUIRE_FALSE(subscriber.is_overrun());
  }
}

struct CopyPolicy
{
  using MsgType = TestMsg;
  static constexpr auto name = "CopyPolicy";
  static constexpr auto endpoint_id =
    jewels::Uuid<common::EndpointClassId>::from_string("27e38987-2e30-497f-9349-bf8d98931470").value();
  static constexpr auto max_view_size = 3U;
  static constexpr auto copy_inputs = true;
  static constexpr auto manual_cursor = false;
  /// Testing only
  static constexpr auto channel_size = 3U;
};

using CopyFixture = InputViewFixture<CopyPolicy>;

TEST_CASE_METHOD(CopyFixture, "no messages", "[max_view_size=3, copy=false, manual=false]")
{
  REQUIRE(subscriber.validate());

  // The view should be empty before any messages are published

  auto input = subscriber.make_dial_input(new_msgs_max_limit);
  REQUIRE(input);
  REQUIRE(input->get_cursor() == input->end());

  // Updating the last consumed shoudl be a no-op.

  std::ignore = subscriber.commit(*input);

  // The view should be empty since no messages are published.

  input = subscriber.make_dial_input(new_msgs_max_limit);
  REQUIRE(input);
  REQUIRE(input->get_cursor() == input->end());
}

TEST_CASE_METHOD(CopyFixture, "publish one message", "[max_view_size=3, copy=true, manual=false]")
{
  REQUIRE(subscriber.validate());

  // Publish message.

  auto msg = TestMsg{.value = 0};
  const auto* msg_ptr = publish_and_return_ptr(msg);

  // The inputs view should only contain the message.

  auto input = subscriber.make_dial_input(new_msgs_max_limit);
  REQUIRE(input);
  auto cursor = input->get_cursor();
  REQUIRE(cursor != input->end());
  REQUIRE(msg == *cursor);
  REQUIRE(input->end() == std::next(cursor));

  // Verify the input message is not pointing to the original publish location (i.e. copied).

  REQUIRE(msg_ptr != &(*cursor));
}

TEST_CASE_METHOD(CopyFixture, "view max msgs", "[max_view_size=3, channel_size=3, copy=true, manual=false]")
{
  REQUIRE(subscriber.validate());

  // Publish message.

  auto msg0 = TestMsg{.value = 0};
  auto msg1 = TestMsg{.value = 1};
  auto msg2 = TestMsg{.value = 2};
  auto msg3 = TestMsg{.value = 3};

  const auto* msg0_ptr = publish_and_return_ptr(msg0);
  REQUIRE(msg0_ptr != nullptr);

  const auto* msg1_ptr = publish_and_return_ptr(msg1);
  const auto* msg2_ptr = publish_and_return_ptr(msg2);
  const auto* msg3_ptr = publish_and_return_ptr(msg3);

  // The inputs view should only contain the message.

  auto input = subscriber.make_dial_input(new_msgs_max_limit);
  REQUIRE(input);

  auto cursor = input->get_cursor();
  REQUIRE(cursor != input->end());
  REQUIRE(msg1 == *cursor);
  REQUIRE(msg1_ptr != &(*cursor));

  cursor = std::next(cursor);
  REQUIRE(cursor != input->end());
  REQUIRE(msg2 == *cursor);
  REQUIRE(msg2_ptr != &(*cursor));

  cursor = std::next(cursor);
  REQUIRE(cursor != input->end());
  REQUIRE(msg3 == *cursor);
  REQUIRE(msg3_ptr != &(*cursor));

  cursor = std::next(cursor);
  REQUIRE(cursor == input->end());
}

TEST_CASE_METHOD(CopyFixture, "overrun", "[max_view_size=3, channel_size=3, copy=true, manual=false]")
{
  REQUIRE(subscriber.validate());

  // Publish message.

  auto msg0 = TestMsg{.value = 0};
  auto msg1 = TestMsg{.value = 1};
  auto msg2 = TestMsg{.value = 2};
  auto msg3 = TestMsg{.value = 3};

  publish(msg0);

  auto input = subscriber.make_dial_input(new_msgs_max_limit);
  REQUIRE(input);

  REQUIRE_FALSE(subscriber.is_overrun());

  publish(msg1);

  REQUIRE_FALSE(subscriber.is_overrun());

  publish(msg2);

  REQUIRE_FALSE(subscriber.is_overrun());

  publish(msg3);

  // Copyied views can never overrun

  REQUIRE_FALSE(subscriber.is_overrun());

  SECTION("clear after commit()")
  {
    std::ignore = subscriber.commit(*input);
    REQUIRE_FALSE(subscriber.is_overrun());
  }

  SECTION("clear after make_dial_input()")
  {
    input = subscriber.make_dial_input(new_msgs_max_limit);
    REQUIRE(input);

    REQUIRE_FALSE(subscriber.is_overrun());
  }
}

struct ManualCursorPolicy
{
  using MsgType = TestMsg;
  static constexpr auto name = "ManualCursorPolicy";
  static constexpr auto endpoint_id =
    jewels::Uuid<common::EndpointClassId>::from_string("26da678b-5b37-442a-b78e-9fc91784e9d4").value();
  static constexpr auto max_view_size = 3U;
  static constexpr auto copy_inputs = false;
  static constexpr auto manual_cursor = true;
  /// Testing only
  static constexpr auto channel_size = 3U;
};

using ManualCursorFixture = InputViewFixture<ManualCursorPolicy>;

TEST_CASE_METHOD(ManualCursorFixture, "no messages", "[max_view_size=3, channel_size=3, copy=false, manual=true]")
{
  REQUIRE(subscriber.validate());

  // The view should be empty before any messages are published

  auto input = subscriber.make_dial_input(new_msgs_max_limit);
  REQUIRE(input);
  REQUIRE(input->get_cursor() == input->end());

  // Updating the last consumed shoudl be a no-op.

  auto last_consumed = subscriber.commit(*input);
  REQUIRE(ManualCursorFixture::Policy::endpoint_id == std::get<0>(last_consumed));
  REQUIRE(pinion::BufferIterator{} == std::get<1>(last_consumed));

  // The view should be empty since no messages are published.

  input = subscriber.make_dial_input(new_msgs_max_limit);
  REQUIRE(input);
  REQUIRE(input->get_cursor() == input->end());
}

TEST_CASE_METHOD(
  ManualCursorFixture, "cursor does not auto update", "[max_view_size=3, channel_size=3, copy=false, manual=true]")
{
  REQUIRE(subscriber.validate());

  // Publish message.

  auto msg0 = TestMsg{.value = 0};

  const auto* msg0_ptr = publish_and_return_ptr(msg0);
  REQUIRE(msg0_ptr != nullptr);

  auto input = subscriber.make_dial_input(new_msgs_max_limit);
  REQUIRE(input);

  // Cursor should start at the beginning.

  REQUIRE(1 == input->get_view().size());
  REQUIRE(1 == input->get_cursor_view().size());
  REQUIRE(input->get_view().begin() == input->get_cursor());
  REQUIRE(input->get_view().begin() == input->get_first_new());

  // Commit the input.

  std::ignore = subscriber.commit(*input);

  // Input should move the new iterator but not the cursor.

  input = subscriber.make_dial_input(new_msgs_max_limit);
  REQUIRE(input);

  REQUIRE(1 == input->get_view().size());
  REQUIRE(1 == input->get_cursor_view().size());
  REQUIRE(input->get_view().begin() == input->get_cursor());
  REQUIRE(input->get_view().end() == input->get_first_new());

  // Update the cursor.

  input->set_cursor(std::next(input->get_cursor()));
  REQUIRE(input->get_view().end() == input->get_cursor());

  // Commit the new cursor.

  std::ignore = subscriber.commit(*input);

  // Input should have the updated cursor.

  input = subscriber.make_dial_input(new_msgs_max_limit);
  REQUIRE(input);

  REQUIRE(1 == input->get_view().size());
  REQUIRE(input->get_cursor_view().empty());
  REQUIRE(input->get_view().end() == input->get_cursor());
  REQUIRE(input->get_view().end() == input->get_first_new());

  // Ensure the cursor is updated to begin if it falls behind view.

  for (size_t i = 0; i <= ManualCursorPolicy::max_view_size; ++i)
  {
    auto msg = TestMsg{.value = static_cast<int32_t>(i)};
    publish(msg);
  }

  input = subscriber.make_dial_input(new_msgs_max_limit);
  REQUIRE(input);

  REQUIRE(3 == input->get_view().size());
  REQUIRE(3 == input->get_cursor_view().size());
  REQUIRE(input->get_view().begin() == input->get_cursor());
  REQUIRE(input->get_view().begin() == input->get_first_new());
}

struct View1Channel3Policy
{
  using MsgType = TestMsg;
  static constexpr auto name = "View1Channel1Policy";
  static constexpr auto endpoint_id =
    jewels::Uuid<common::EndpointClassId>::from_string("27e38987-2e30-497f-9349-bf8d98931470").value();
  static constexpr auto max_view_size = 1U;
  static constexpr auto copy_inputs = false;
  static constexpr auto manual_cursor = false;
  /// Testing only
  static constexpr auto channel_size = 1U;
};

using View1Channel1Fixture = InputViewFixture<View1Channel3Policy>;

TEST_CASE_METHOD(
  View1Channel1Fixture,
  "view correct after commit with no new messages",
  "[max_view_size=1, channel_size=1, copy=false, manual=false]")
{
  REQUIRE(subscriber.validate());

  // Publish message.

  auto msg = TestMsg{.value = 0};
  publish(msg);
  publish(msg);
  publish(msg);

  // The inputs view should only contain the message.

  auto input = subscriber.make_dial_input(new_msgs_max_limit);
  REQUIRE(input);
  auto cursor = input->get_cursor();
  REQUIRE(cursor != input->end());
  REQUIRE(msg == *cursor);
  REQUIRE(input->end() == std::next(cursor));
  REQUIRE(1 == input->get_view().size());
  REQUIRE(1 == input->get_new_msgs_view().size());

  // Commit the view.

  std::ignore = subscriber.commit(*input);

  // Make inputs with no new messages. Cursor view should still be one but cursor points to the end.

  input = subscriber.make_dial_input(0);
  REQUIRE(input);
  REQUIRE(1 == input->get_view().size());
  REQUIRE(input->get_new_msgs_view().empty());
  REQUIRE(input->get_cursor() == input->end());

  // Commit the view.

  std::ignore = subscriber.commit(*input);

  // Make inputs with no new messages. Cursor view should still be one but cursor points to the end.

  input = subscriber.make_dial_input(0);
  REQUIRE(input);
  REQUIRE(1 == input->get_view().size());
  REQUIRE(input->get_cursor_view().empty());
  REQUIRE(input->get_new_msgs_view().empty());
  REQUIRE(input->get_cursor() == input->end());
}

struct InvalidChannelSizePolicy
{
  using MsgType = TestMsg;
  static constexpr auto name = "InvalidChannelSizePolicy";
  static constexpr auto endpoint_id =
    jewels::Uuid<common::EndpointClassId>::from_string("cf199401-65d4-4ddd-8b1c-4c1d2ea9c38c").value();
  static constexpr auto max_view_size = 10U;
  static constexpr auto copy_inputs = false;
  static constexpr auto manual_cursor = false;
  /// Testing only
  static constexpr auto channel_size = 1U;
};

using InvalidChannelSizeFixture = InputViewFixture<InvalidChannelSizePolicy>;

TEST_CASE_METHOD(
  InvalidChannelSizeFixture,
  "invalid view/channel size pair fails validation",
  "[max_view_size=10, channel_size=1, copy=false, manual=false]")
{
  // Change this to false once its actually an error.
  REQUIRE(subscriber.validate());
}

} // namespace
} // namespace clockwork
