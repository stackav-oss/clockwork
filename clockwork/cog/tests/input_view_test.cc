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
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <gsl/util>

#include <chrono>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory_resource>
#include <optional>
#include <ranges>
#include <sys/types.h>
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

template <typename PolicyType, bool offline = false>
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
      subscriber(subscriber_handle, 10, resource, offline)
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
  void publish(const MsgType& msg, jewels::time::SyncTime publish_time = fake_publish_time)
  {
    auto slot = publisher_handle.reserve().value();
    auto publishable = pinion::Publishable<MsgType>::try_make(jewels::memory::make_non_null_from_ref(slot)).value();
    publishable.message() = msg;

    REQUIRE(slot.commit(publish_time));
  }
};

struct NoCopyPolicy
{
  using MsgType = TestMsg;
  static constexpr auto name = "NoCopyPolicy";
  static constexpr auto endpoint_id =
    jewels::Uuid<common::EndpointClassId>::from_string("27e38987-2e30-497f-9349-bf8d98931470").value();
  static constexpr auto max_view_size = 3U;
  static constexpr std::optional<::ssize_t> safety_margin{};
  static constexpr std::optional<size_t> skip_threshold{};
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

  auto input = subscriber.make_dial_input(new_msgs_max_limit, jewels::time::SyncTime());
  REQUIRE(input);
  REQUIRE(input->get_cursor() == input->end());

  // Updating the last consumed shoudl be a no-op.

  auto last_consumed = subscriber.commit(*input);
  REQUIRE(NoCopyFixture::Policy::endpoint_id == std::get<0>(last_consumed));
  REQUIRE(pinion::BufferIterator{} == std::get<1>(last_consumed));

  // The view should be empty since no messages are published.

  input = subscriber.make_dial_input(new_msgs_max_limit, jewels::time::SyncTime());
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

  auto input = subscriber.make_dial_input(new_msgs_max_limit, jewels::time::SyncTime());
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

  auto input = subscriber.make_dial_input(new_msgs_max_limit, jewels::time::SyncTime());
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

  input = subscriber.make_dial_input(0, jewels::time::SyncTime());
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

  auto input = subscriber.make_dial_input(new_msgs_max_limit, jewels::time::SyncTime());
  REQUIRE(input);
  REQUIRE(input->get_cursor_view().empty());

  // Publish one message

  publish(msg0);

  input = subscriber.make_dial_input(new_msgs_max_limit, jewels::time::SyncTime());
  REQUIRE(input);
  REQUIRE(1 == input->get_cursor_view().size());
  REQUIRE(input->get_cursor() != input->end());
  REQUIRE(input->get_first_new() == input->get_cursor());
  REQUIRE(msg0 == *input->get_cursor());
  REQUIRE(msg0 == *input->get_first_new());

  // Update last consumed, first new is end

  std::ignore = subscriber.commit(*input);

  input = subscriber.make_dial_input(new_msgs_max_limit, jewels::time::SyncTime());
  REQUIRE(input);
  REQUIRE(input->get_cursor_view().empty());
  REQUIRE(input->get_cursor() == input->end());
  REQUIRE(input->get_first_new() == input->end());

  // Publish two messages, first new is msg1

  publish(msg1);
  publish(msg2);

  input = subscriber.make_dial_input(new_msgs_max_limit, jewels::time::SyncTime());
  REQUIRE(input);
  REQUIRE(2 == input->get_cursor_view().size());
  REQUIRE(input->get_cursor() != input->end());
  REQUIRE(input->get_first_new() != input->end());
  REQUIRE(msg1 == *input->get_cursor());
  REQUIRE(msg1 == *input->get_first_new());

  // Publish last message, first new is still msg1

  publish(msg3);

  input = subscriber.make_dial_input(new_msgs_max_limit, jewels::time::SyncTime());
  REQUIRE(input);
  REQUIRE(3 == input->get_cursor_view().size());
  REQUIRE(input->get_cursor() != input->end());
  REQUIRE(input->get_first_new() == input->get_cursor());
  REQUIRE(input->get_first_new() != input->end());
  REQUIRE(msg1 == *input->get_cursor());
  REQUIRE(msg1 == *input->get_first_new());

  // Update last consumed, first new is end

  std::ignore = subscriber.commit(*input);

  input = subscriber.make_dial_input(new_msgs_max_limit, jewels::time::SyncTime());
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

  auto input = subscriber.make_dial_input(new_msgs_max_limit, jewels::time::SyncTime());
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
  input = subscriber.make_dial_input(new_msgs_max_limit, jewels::time::SyncTime());
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

  auto input = subscriber.make_dial_input(new_msgs_max_limit, jewels::time::SyncTime());
  REQUIRE(input);

  REQUIRE_FALSE(subscriber.is_overrun());
  REQUIRE_FALSE(subscriber.almost_overrun());

  publish(msg1);

  REQUIRE_FALSE(subscriber.is_overrun());
  REQUIRE(subscriber.almost_overrun());

  publish(msg2);

  REQUIRE_FALSE(subscriber.is_overrun());
  REQUIRE(subscriber.almost_overrun());

  publish(msg3);

  REQUIRE(subscriber.is_overrun());
  REQUIRE(subscriber.almost_overrun());

  SECTION("clear after commit()")
  {
    std::ignore = subscriber.commit(*input);
    REQUIRE_FALSE(subscriber.is_overrun());
  }

  SECTION("clear after make_dial_input()")
  {
    input = subscriber.make_dial_input(new_msgs_max_limit, jewels::time::SyncTime());
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
  static constexpr std::optional<::ssize_t> safety_margin{};
  static constexpr std::optional<size_t> skip_threshold{};
  static constexpr auto copy_inputs = true;
  static constexpr auto manual_cursor = false;
  /// Testing only
  static constexpr auto channel_size = 3U;
};

using CopyFixture = InputViewFixture<CopyPolicy>;


/// First, set up an InputView test fixture.
TEST_CASE_METHOD(CopyFixture, "copy inputs", "[max_view_size=3, copy=false, manual=false]")
{
  REQUIRE(subscriber.validate());

  SECTION("no messages")
  {
    // Step 2: Check that the view is empty before any messages are published
    auto input = subscriber.make_dial_input(new_msgs_max_limit, jewels::time::SyncTime());
    REQUIRE(input);
    REQUIRE(input->get_cursor() == input->end());

    // Step 3: Commit the view. Updating the last consumed iterator should be a no-op.
    std::ignore = subscriber.commit(*input);

    // Step 4: Confirm that the view is still empty. No messages were published.
    input = subscriber.make_dial_input(new_msgs_max_limit, jewels::time::SyncTime());
    REQUIRE(input);
    REQUIRE(input->get_cursor() == input->end());
  }

  SECTION("publish one message")
  {
    // Step 5: Publish a message.
    auto msg = TestMsg{.value = 0};
    const auto* msg_ptr = publish_and_return_ptr(msg);

    // Step 6: Confirm that the input view contains the message.
    auto input = subscriber.make_dial_input(new_msgs_max_limit, jewels::time::SyncTime());
    REQUIRE(input);
    auto cursor = input->get_cursor();
    REQUIRE(cursor != input->end());
    REQUIRE(msg == *cursor);
    REQUIRE(input->end() == std::next(cursor));

    // Step 7: Confirm that input view iterator is not pointing to the address of the original message (i.e. copied).
    REQUIRE(msg_ptr != &(*cursor));
  }

  SECTION("view max msgs")
  {
    auto msg0 = TestMsg{.value = 0};
    auto msg1 = TestMsg{.value = 1};
    auto msg2 = TestMsg{.value = 2};
    auto msg3 = TestMsg{.value = 3};

    // Step 8: publish enough messages to fill the view and wrap around by one.
    const auto* msg0_ptr = publish_and_return_ptr(msg0);
    REQUIRE(msg0_ptr != nullptr);

    const auto* msg1_ptr = publish_and_return_ptr(msg1);
    const auto* msg2_ptr = publish_and_return_ptr(msg2);
    const auto* msg3_ptr = publish_and_return_ptr(msg3);

    // Step 9: Check that the view only contains the three latest messages.
    auto input = subscriber.make_dial_input(new_msgs_max_limit, jewels::time::SyncTime());
    REQUIRE(input);

    // Step 10: Confirm that each message in the view is not pointing to the address of its original message.
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

  SECTION("overrun")
  {
    auto msg0 = TestMsg{.value = 0};
    auto msg1 = TestMsg{.value = 1};
    auto msg2 = TestMsg{.value = 2};
    auto msg3 = TestMsg{.value = 3};

    // Step 11: publish one message and construct a dial from the view. Confirm that the view reports it hasn't been
    // overrun.
    publish(msg0);

    auto input = subscriber.make_dial_input(new_msgs_max_limit, jewels::time::SyncTime());
    REQUIRE(input);
    REQUIRE_FALSE(subscriber.is_overrun());

    // Step 12: Publish enough messages to fill the view and wrap around by one. After each publish confirm that the
    // view does not report an overrun.
    publish(msg1);
    REQUIRE_FALSE(subscriber.is_overrun());

    publish(msg2);
    REQUIRE_FALSE(subscriber.is_overrun());

    publish(msg3);
    REQUIRE_FALSE(subscriber.is_overrun());

    // Step 13: Commit the view and confirm that it still doesn't report an overrun.
    std::ignore = subscriber.commit(*input);
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
  static constexpr std::optional<::ssize_t> safety_margin{};
  static constexpr std::optional<size_t> skip_threshold{};
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

  auto input = subscriber.make_dial_input(new_msgs_max_limit, jewels::time::SyncTime());
  REQUIRE(input);
  REQUIRE(input->get_cursor() == input->end());

  // Updating the last consumed shoudl be a no-op.

  auto last_consumed = subscriber.commit(*input);
  REQUIRE(ManualCursorFixture::Policy::endpoint_id == std::get<0>(last_consumed));
  REQUIRE(pinion::BufferIterator{} == std::get<1>(last_consumed));

  // The view should be empty since no messages are published.

  input = subscriber.make_dial_input(new_msgs_max_limit, jewels::time::SyncTime());
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

  auto input = subscriber.make_dial_input(new_msgs_max_limit, jewels::time::SyncTime());
  REQUIRE(input);

  // Cursor should start at the beginning.

  REQUIRE(1 == input->get_view().size());
  REQUIRE(1 == input->get_cursor_view().size());
  REQUIRE(input->get_view().begin() == input->get_cursor());
  REQUIRE(input->get_view().begin() == input->get_first_new());

  // Commit the input.

  std::ignore = subscriber.commit(*input);

  // Input should move the new iterator but not the cursor.

  input = subscriber.make_dial_input(new_msgs_max_limit, jewels::time::SyncTime());
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

  input = subscriber.make_dial_input(new_msgs_max_limit, jewels::time::SyncTime());
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

  input = subscriber.make_dial_input(new_msgs_max_limit, jewels::time::SyncTime());
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
  static constexpr std::optional<::ssize_t> safety_margin{};
  static constexpr std::optional<size_t> skip_threshold{};
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

  auto input = subscriber.make_dial_input(new_msgs_max_limit, jewels::time::SyncTime());
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

  input = subscriber.make_dial_input(0, jewels::time::SyncTime());
  REQUIRE(input);
  REQUIRE(1 == input->get_view().size());
  REQUIRE(input->get_new_msgs_view().empty());
  REQUIRE(input->get_cursor() == input->end());

  // Commit the view.

  std::ignore = subscriber.commit(*input);

  // Make inputs with no new messages. Cursor view should still be one but cursor points to the end.

  input = subscriber.make_dial_input(0, jewels::time::SyncTime());
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
  static constexpr std::optional<::ssize_t> safety_margin{};
  static constexpr std::optional<size_t> skip_threshold{};
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

struct SafetyMarginPolicy
{
  using MsgType = TestMsg;
  static constexpr auto name = "SafetyMarginPolicy";
  static constexpr auto endpoint_id =
    jewels::Uuid<common::EndpointClassId>::from_string("19712716-7b7a-4ff1-ab64-26ce1b43ad09").value();
  static constexpr auto max_view_size = 3U;
  static constexpr std::optional<::ssize_t> safety_margin{3U};
  static constexpr std::optional<size_t> skip_threshold{};
  static constexpr auto copy_inputs = false;
  static constexpr auto manual_cursor = false;
  /// Testing only
  static constexpr auto channel_size = 7U;
};

using OnlineSafetyMarginFixture = InputViewFixture<SafetyMarginPolicy, false>;
using OfflineSafetyMarginFixture = InputViewFixture<SafetyMarginPolicy, true>;


/// First, set up an InputView test fixture.
TEST_CASE_METHOD(
  OnlineSafetyMarginFixture,
  "skip to maintain safety margin",
  "[max_view_size=3, safety_margin=3, channel_size=7, copy=false, manual=false]")
{
  constexpr auto max_new = PinionDifferenceType{SafetyMarginPolicy::max_view_size};
  REQUIRE(subscriber.validate());

  auto msg0 = TestMsg{.value = 0};
  auto msg1 = TestMsg{.value = 1};
  auto msg2 = TestMsg{.value = 2};
  auto msg3 = TestMsg{.value = 3};
  auto msg4 = TestMsg{.value = 4};
  auto msg5 = TestMsg{.value = 5};
  auto msg6 = TestMsg{.value = 6};

  // Step 1: Publish three messages to the channel.
  std::ignore = publish_and_return_ptr(msg0);
  std::ignore = publish_and_return_ptr(msg1);
  std::ignore = publish_and_return_ptr(msg2);

  // Step 2: Construct a dial from the view and confirm that it contains all three messsages.
  auto input = subscriber.make_dial_input(max_new, jewels::time::SyncTime());
  REQUIRE(input);

  auto cursor = input->get_cursor();
  REQUIRE(cursor != input->end());
  REQUIRE(msg0 == *cursor);
  REQUIRE(cursor == input->get_first_new());
  cursor = std::next(cursor);
  REQUIRE(cursor != input->end());
  REQUIRE(msg1 == *cursor);
  cursor = std::next(cursor);
  REQUIRE(cursor != input->end());
  REQUIRE(msg2 == *cursor);

  // Step 3: Publish 3 more messages.
  std::ignore = publish_and_return_ptr(msg3);
  std::ignore = publish_and_return_ptr(msg4);
  std::ignore = publish_and_return_ptr(msg5);

  // Step 2: Construct a dial from the view and confirm that it contains all new messsages.
  input = subscriber.make_dial_input(max_new, jewels::time::SyncTime());
  REQUIRE(input);

  cursor = input->get_cursor();
  REQUIRE(cursor != input->end());
  REQUIRE(msg3 == *cursor);
  cursor = std::next(cursor);
  REQUIRE(cursor != input->end());
  REQUIRE(msg4 == *cursor);
  cursor = std::next(cursor);
  REQUIRE(cursor != input->end());
  REQUIRE(msg5 == *cursor);
  std::ignore = subscriber.commit(*input);

  // Step 4: Simulate a burst from the publisher by publishing five messages. This should breach the safety margin.
  std::ignore = publish_and_return_ptr(msg6);
  std::ignore = publish_and_return_ptr(msg0);
  std::ignore = publish_and_return_ptr(msg1);
  std::ignore = publish_and_return_ptr(msg2);
  std::ignore = publish_and_return_ptr(msg3);

  // Step 5: Construct a dial from the view and confirm that it contains only the three newest messsages.
  input = subscriber.make_dial_input(max_new, jewels::time::SyncTime());
  REQUIRE(input);

  // The view should jump to the newest messages.
  cursor = input->get_cursor();
  REQUIRE(cursor != input->end());
  REQUIRE(msg1 == *cursor);
  REQUIRE(cursor == input->get_first_new());
  cursor = std::next(cursor);
  REQUIRE(cursor != input->end());
  REQUIRE(msg2 == *cursor);
  cursor = std::next(cursor);
  REQUIRE(cursor != input->end());
  REQUIRE(msg3 == *cursor);
  cursor = std::next(cursor);
  REQUIRE(cursor == input->end());

  // Step 6: Confirm that the view reports the safety skip flag for diagnostics.
  REQUIRE(subscriber.did_safety_skip());
}


/// First, set up an InputView test fixture.
TEST_CASE_METHOD(
  OnlineSafetyMarginFixture,
  "fast forward on first exec",
  "[max_view_size=3, safety_margin=3, channel_size=7, copy=false, manual=false]")
{
  constexpr auto max_new = PinionDifferenceType{SafetyMarginPolicy::max_view_size};
  REQUIRE(subscriber.validate());

  auto msg0 = TestMsg{.value = 0};
  auto msg1 = TestMsg{.value = 1};
  auto msg2 = TestMsg{.value = 2};
  auto msg3 = TestMsg{.value = 3};
  auto msg4 = TestMsg{.value = 4};
  auto msg5 = TestMsg{.value = 5};
  auto msg6 = TestMsg{.value = 6};

  // Step 1: Publish enough messages to fill the channel.
  std::ignore = publish_and_return_ptr(msg0);
  std::ignore = publish_and_return_ptr(msg1);
  std::ignore = publish_and_return_ptr(msg2);
  std::ignore = publish_and_return_ptr(msg3);
  std::ignore = publish_and_return_ptr(msg4);
  std::ignore = publish_and_return_ptr(msg5);
  std::ignore = publish_and_return_ptr(msg6);

  // Step 2: Construct a dial from the input view.
  auto input = subscriber.make_dial_input(max_new, jewels::time::SyncTime());
  REQUIRE(input);

  // Step 3: Confirm that the view only conatins the three most recent messsages.
  auto cursor = input->get_cursor();
  REQUIRE(cursor != input->end());
  REQUIRE(msg4 == *cursor);
  REQUIRE(cursor == input->get_first_new());
  cursor = std::next(cursor);
  REQUIRE(cursor != input->end());
  REQUIRE(msg5 == *cursor);
  cursor = std::next(cursor);
  REQUIRE(cursor != input->end());
  REQUIRE(msg6 == *cursor);
}

TEST_CASE_METHOD(
  OfflineSafetyMarginFixture,
  "no fast forward on first exec",
  "[max_view_size=3, safety_margin=3, channel_size=7, copy=false, manual=false]")
{
  constexpr auto max_new = PinionDifferenceType{SafetyMarginPolicy::max_view_size};
  REQUIRE(subscriber.validate());

  auto msg0 = TestMsg{.value = 0};
  auto msg1 = TestMsg{.value = 1};
  auto msg2 = TestMsg{.value = 2};
  auto msg3 = TestMsg{.value = 3};
  auto msg4 = TestMsg{.value = 4};
  auto msg5 = TestMsg{.value = 5};
  auto msg6 = TestMsg{.value = 6};

  // Publish some messages to the buffer.
  std::ignore = publish_and_return_ptr(msg0);
  std::ignore = publish_and_return_ptr(msg1);
  std::ignore = publish_and_return_ptr(msg2);
  std::ignore = publish_and_return_ptr(msg3);
  std::ignore = publish_and_return_ptr(msg4);
  std::ignore = publish_and_return_ptr(msg5);
  std::ignore = publish_and_return_ptr(msg6);

  // The view should contain the earliest messsages.
  auto input = subscriber.make_dial_input(max_new, jewels::time::SyncTime());
  REQUIRE(input);

  auto cursor = input->get_cursor();
  REQUIRE(cursor != input->end());
  REQUIRE(msg0 == *cursor);
  REQUIRE(cursor == input->get_first_new());
  cursor = std::next(cursor);
  REQUIRE(cursor != input->end());
  REQUIRE(msg1 == *cursor);
  cursor = std::next(cursor);
  REQUIRE(cursor != input->end());
  REQUIRE(msg2 == *cursor);
}

TEST_CASE_METHOD(
  OfflineSafetyMarginFixture,
  "no safety margin skip",
  "[max_view_size=3, safety_margin=3, channel_size=7, copy=false, manual=false]")
{
  constexpr auto max_new = PinionDifferenceType{SafetyMarginPolicy::max_view_size};
  REQUIRE(subscriber.validate());

  auto msg0 = TestMsg{.value = 0};
  auto msg1 = TestMsg{.value = 1};
  auto msg2 = TestMsg{.value = 2};
  auto msg3 = TestMsg{.value = 3};
  auto msg4 = TestMsg{.value = 4};
  auto msg5 = TestMsg{.value = 5};
  auto msg6 = TestMsg{.value = 6};

  // Publish some messages to the buffer.
  std::ignore = publish_and_return_ptr(msg0);
  std::ignore = publish_and_return_ptr(msg1);
  std::ignore = publish_and_return_ptr(msg2);

  // The view should contain the three most recent messsages.
  auto input = subscriber.make_dial_input(max_new, jewels::time::SyncTime());
  REQUIRE(input);
  REQUIRE(input->num_messages_skipped() == 0);

  auto cursor = input->get_cursor();
  REQUIRE(cursor != input->end());
  REQUIRE(msg0 == *cursor);
  REQUIRE(cursor == input->get_first_new());
  cursor = std::next(cursor);
  REQUIRE(cursor != input->end());
  REQUIRE(msg1 == *cursor);
  cursor = std::next(cursor);
  REQUIRE(cursor != input->end());
  REQUIRE(msg2 == *cursor);

  std::ignore = subscriber.commit(*input);

  // Slide forward a bit.
  std::ignore = publish_and_return_ptr(msg3);
  std::ignore = publish_and_return_ptr(msg4);
  std::ignore = publish_and_return_ptr(msg5);

  input = subscriber.make_dial_input(max_new, jewels::time::SyncTime());
  REQUIRE(input);

  cursor = input->get_cursor();
  REQUIRE(cursor != input->end());
  REQUIRE(msg3 == *cursor);
  cursor = std::next(cursor);
  REQUIRE(cursor != input->end());
  REQUIRE(msg4 == *cursor);
  cursor = std::next(cursor);
  REQUIRE(cursor != input->end());
  REQUIRE(msg5 == *cursor);
  std::ignore = subscriber.commit(*input);

  // Simulate a burst from the publisher. Publish enough to breach the margin.
  std::ignore = publish_and_return_ptr(msg6);
  std::ignore = publish_and_return_ptr(msg0);
  std::ignore = publish_and_return_ptr(msg1);
  std::ignore = publish_and_return_ptr(msg2);
  std::ignore = publish_and_return_ptr(msg3);

  input = subscriber.make_dial_input(max_new, jewels::time::SyncTime());
  REQUIRE(input);

  // Since we're executing offline, and thus aren't applying the saftey margin
  // policy, the view should move like it normally would.
  cursor = input->get_cursor();
  REQUIRE(cursor != input->end());
  REQUIRE(msg6 == *cursor);
  cursor = std::next(cursor);
  REQUIRE(cursor != input->end());
  REQUIRE(msg0 == *cursor);
  cursor = std::next(cursor);
  REQUIRE(cursor != input->end());
  REQUIRE(msg1 == *cursor);
}

struct SkipThresholdPolicy
{
  using MsgType = TestMsg;
  static constexpr auto name = "SafetyMarginPolicy";
  static constexpr auto endpoint_id =
    jewels::Uuid<common::EndpointClassId>::from_string("19712716-7b7a-4ff1-ab64-26ce1b43ad09").value();
  static constexpr auto max_view_size = 3U;
  static constexpr std::optional<size_t> safety_margin{};
  static constexpr std::optional<size_t> skip_threshold{10U};
  static constexpr auto copy_inputs = false;
  static constexpr auto manual_cursor = false;
  /// Testing only
  static constexpr auto channel_size = 20U;
};

using SkipThresholdFixture = InputViewFixture<SkipThresholdPolicy>;

TEST_CASE_METHOD(
  SkipThresholdFixture,
  "preemptive skip",
  "[max_view_size=3, skip_threshold=10, channel_size=20, copy=false, manual=false]")
{
  constexpr auto max_new = PinionDifferenceType{SafetyMarginPolicy::max_view_size};
  REQUIRE(subscriber.validate());

  // Publish some messages to the buffer.
  auto msg0 = TestMsg{.value = 0};
  auto msg1 = TestMsg{.value = 1};
  auto msg2 = TestMsg{.value = 2};
  auto msg3 = TestMsg{.value = 3};

  std::ignore = publish_and_return_ptr(msg0);
  auto input = subscriber.make_dial_input(max_new, jewels::time::SyncTime());
  REQUIRE(input);
  std::ignore = subscriber.commit(*input);

  std::ignore = publish_and_return_ptr(msg1);
  std::ignore = publish_and_return_ptr(msg2);
  std::ignore = publish_and_return_ptr(msg3);

  // The view should contain the first three messsages.
  input = subscriber.make_dial_input(max_new, jewels::time::SyncTime());
  REQUIRE(input);

  auto cursor = input->get_cursor();
  REQUIRE(cursor != input->end());
  REQUIRE(msg1 == *cursor);
  REQUIRE(cursor == input->get_first_new());
  cursor = std::next(cursor);
  REQUIRE(cursor != input->end());
  REQUIRE(msg2 == *cursor);
  cursor = std::next(cursor);
  REQUIRE(cursor != input->end());
  REQUIRE(msg3 == *cursor);

  // Publish some spam
  auto spam = TestMsg{.value = 99};
  for (auto i = 0; i < 5; i++)
  {
    std::ignore = publish_and_return_ptr(spam);
  }

  // The input view should not move. We haven't fallen far enough behind.
  input = subscriber.make_dial_input(max_new, jewels::time::SyncTime());
  REQUIRE(input);
  REQUIRE(input->num_messages_skipped() == 0);

  cursor = input->get_cursor();
  REQUIRE(cursor != input->end());
  REQUIRE(msg1 == *cursor);
  cursor = std::next(cursor);
  REQUIRE(cursor != input->end());
  REQUIRE(msg2 == *cursor);
  cursor = std::next(cursor);
  REQUIRE(cursor != input->end());
  REQUIRE(msg3 == *cursor);

  // Publish a few more times to trigger a preemptive skip.
  std::ignore = publish_and_return_ptr(spam);
  std::ignore = publish_and_return_ptr(spam);
  auto msg4 = TestMsg{.value = 4};
  auto msg5 = TestMsg{.value = 5};
  auto msg6 = TestMsg{.value = 6};
  std::ignore = publish_and_return_ptr(msg4);
  std::ignore = publish_and_return_ptr(msg5);
  std::ignore = publish_and_return_ptr(msg6);

  input = subscriber.make_dial_input(max_new, jewels::time::SyncTime());
  REQUIRE(input);
  REQUIRE(input->num_messages_skipped() == 10);

  cursor = input->get_cursor();
  REQUIRE(cursor != input->end());
  REQUIRE(msg4 == *cursor);
  cursor = std::next(cursor);
  REQUIRE(cursor != input->end());
  REQUIRE(msg5 == *cursor);
  cursor = std::next(cursor);
  REQUIRE(cursor != input->end());
  REQUIRE(msg6 == *cursor);

  std::ignore = subscriber.commit(*input);
  for (auto i = 0; i < 13; i++)
  {
    std::ignore = publish_and_return_ptr(spam);
  }
  std::ignore = publish_and_return_ptr(msg1);
  std::ignore = publish_and_return_ptr(msg2);
  std::ignore = publish_and_return_ptr(msg3);
  input = subscriber.make_dial_input(max_new, jewels::time::SyncTime());
  REQUIRE(input);
  REQUIRE(input->num_messages_skipped() == 13);
  cursor = input->get_cursor();
  REQUIRE(cursor != input->end());
  REQUIRE(msg1 == *cursor);
  cursor = std::next(cursor);
  REQUIRE(cursor != input->end());
  REQUIRE(msg2 == *cursor);
  cursor = std::next(cursor);
  REQUIRE(cursor != input->end());
  REQUIRE(msg3 == *cursor);
}

struct OverrunWarningPolicy
{
  using MsgType = TestMsg;
  static constexpr auto name = "OverrunWarningPolicy";
  static constexpr auto endpoint_id =
    jewels::Uuid<common::EndpointClassId>::from_string("346c2d3f-2c51-425c-a241-d1433d3d2a62").value();
  static constexpr auto max_view_size = 3U;
  static constexpr std::optional<::ssize_t> safety_margin{};
  static constexpr std::optional<size_t> skip_threshold{};
  static constexpr auto copy_inputs = false;
  static constexpr auto manual_cursor = false;
  static constexpr auto channel_size = 10U;
};

using OverrunWarningFixture = InputViewFixture<OverrunWarningPolicy>;

TEST_CASE_METHOD(
  OverrunWarningFixture, "overrun warning", "[max_view_size=3, channel_size=10, copy=false, manual=false]")
{
  constexpr auto max_new = PinionDifferenceType{OverrunWarningPolicy::max_view_size};
  REQUIRE(subscriber.validate());

  // Publish some messages to the buffer.
  auto msg0 = TestMsg{.value = 0};
  auto msg1 = TestMsg{.value = 1};
  auto msg2 = TestMsg{.value = 2};
  std::ignore = publish_and_return_ptr(msg0);
  std::ignore = publish_and_return_ptr(msg1);
  std::ignore = publish_and_return_ptr(msg2);

  // The view should contain the first three messsages.
  auto input = subscriber.make_dial_input(max_new, jewels::time::SyncTime());
  REQUIRE(input);

  auto cursor = input->get_cursor();
  REQUIRE(cursor != input->end());
  REQUIRE(msg0 == *cursor);
  REQUIRE(cursor == input->get_first_new());
  cursor = std::next(cursor);
  REQUIRE(cursor != input->end());
  REQUIRE(msg1 == *cursor);
  cursor = std::next(cursor);
  REQUIRE(cursor != input->end());
  REQUIRE(msg2 == *cursor);

  // Publish some spam
  auto spam = TestMsg{.value = 99};
  for (auto i = 0; i < 5; i++)
  {
    std::ignore = publish_and_return_ptr(spam);
  }
  // There are still three messages between the publisher and subscriber. Plenty of breathing room.
  REQUIRE_FALSE(subscriber.almost_overrun());
  std::ignore = publish_and_return_ptr(spam);
  // Not any more.
  REQUIRE(subscriber.almost_overrun());
  // Should apply when, and after, the publisher catches up.
  std::ignore = publish_and_return_ptr(spam);
  REQUIRE(subscriber.almost_overrun());
  std::ignore = publish_and_return_ptr(spam);
  REQUIRE(subscriber.almost_overrun());
  REQUIRE(subscriber.is_overrun());
}

struct LargeOverrunWarningPolicy
{
  using MsgType = TestMsg;
  static constexpr auto name = "OverrunWarningPolicy";
  static constexpr auto endpoint_id =
    jewels::Uuid<common::EndpointClassId>::from_string("a1cd4c06-d916-4344-b759-b2e2b7202e25").value();
  static constexpr auto max_view_size = 3U;
  static constexpr std::optional<::ssize_t> safety_margin{};
  static constexpr std::optional<size_t> skip_threshold{};
  static constexpr auto copy_inputs = false;
  static constexpr auto manual_cursor = false;
  static constexpr auto channel_size = 50U;
};

using LargeOverrunWarningFixture = InputViewFixture<LargeOverrunWarningPolicy>;

TEST_CASE_METHOD(
  LargeOverrunWarningFixture,
  "overrun warning with large channel",
  "[max_view_size=3, channel_size=50, copy=false, manual=false]")
{
  constexpr auto max_new = PinionDifferenceType{OverrunWarningPolicy::max_view_size};
  REQUIRE(subscriber.validate());

  // Publish some messages to the buffer.
  auto msg0 = TestMsg{.value = 0};
  auto msg1 = TestMsg{.value = 1};
  auto msg2 = TestMsg{.value = 2};
  std::ignore = publish_and_return_ptr(msg0);
  std::ignore = publish_and_return_ptr(msg1);
  std::ignore = publish_and_return_ptr(msg2);

  // The view should contain the first three messsages.
  auto input = subscriber.make_dial_input(max_new, jewels::time::SyncTime());
  REQUIRE(input);

  auto cursor = input->get_cursor();
  REQUIRE(cursor != input->end());
  REQUIRE(msg0 == *cursor);
  REQUIRE(cursor == input->get_first_new());
  cursor = std::next(cursor);
  REQUIRE(cursor != input->end());
  REQUIRE(msg1 == *cursor);
  cursor = std::next(cursor);
  REQUIRE(cursor != input->end());
  REQUIRE(msg2 == *cursor);

  // Publish some spam
  auto spam = TestMsg{.value = 99};
  for (auto i = 0; i < 42; i++)
  {
    std::ignore = publish_and_return_ptr(spam);
  }
  // This buffer is large enough to require a 10% margin instead of two
  // messages. In this case, there is still a five message gap.
  REQUIRE_FALSE(subscriber.almost_overrun());
  std::ignore = publish_and_return_ptr(spam);
  // Not any more.
  REQUIRE(subscriber.almost_overrun());
  for (auto i = 0; i < 5; i++)
  {
    // Should apply when, and after, the publisher catches up.
    std::ignore = publish_and_return_ptr(spam);
    REQUIRE(subscriber.almost_overrun());
  }
  REQUIRE(subscriber.is_overrun());
}

struct MetricsTestingPolicy
{
  using MsgType = TestMsg;
  static constexpr auto name = "MetricsTestingPolicy";
  static constexpr auto endpoint_id =
    jewels::Uuid<common::EndpointClassId>::from_string("1188a67a-7acd-48c5-9bff-72300745f355").value();
  static constexpr auto max_view_size = 2U;
  static constexpr std::optional<::ssize_t> safety_margin{};
  static constexpr std::optional<size_t> skip_threshold{};
  static constexpr auto copy_inputs = false;
  static constexpr auto manual_cursor = false;
  /// Testing only
  static constexpr auto channel_size = 3U;
};
using MetricsTestingFixture = InputViewFixture<MetricsTestingPolicy>;

TEST_CASE_METHOD(MetricsTestingFixture, "input metrics", "[max_view_size=2, channel_size=3, copy=false, manual=false]")
{
  REQUIRE(subscriber.validate());

  // Initially, metrics should be empty.
  const auto& initial_metrics = subscriber.get_aggregated_input_metrics();
  REQUIRE(initial_metrics.event_metrics.empty());
  REQUIRE_FALSE(initial_metrics.telemetry_metrics.num_unseen_messages.min().has_value());
  REQUIRE_FALSE(initial_metrics.telemetry_metrics.message_staleness.min().has_value());
  REQUIRE_FALSE(initial_metrics.telemetry_metrics.messages_dropped.min().has_value());

  // Publish 3 messages.
  publish(TestMsg{.value = 0}, jewels::time::SyncTime(std::chrono::nanoseconds{15000}));
  publish(TestMsg{.value = 1}, jewels::time::SyncTime(std::chrono::nanoseconds{15000}));
  publish(TestMsg{.value = 2}, jewels::time::SyncTime(std::chrono::nanoseconds{15000}));

  auto input = subscriber.make_dial_input(3, jewels::time::SyncTime(std::chrono::nanoseconds{20000}));
  REQUIRE(input);

  // Check metrics after first call.
  const auto& metrics1 = subscriber.get_aggregated_input_metrics();
  REQUIRE(metrics1.event_metrics.size() == 1);
  REQUIRE(metrics1.event_metrics[0].num_unseen_messages == 3);
  REQUIRE(metrics1.event_metrics[0].messages_dropped == 1);
  REQUIRE(metrics1.event_metrics[0].message_staleness.count() == 500);

  REQUIRE(metrics1.telemetry_metrics.num_unseen_messages.min().has_value());
  REQUIRE(metrics1.telemetry_metrics.num_unseen_messages.min().value() == 3);
  REQUIRE(metrics1.telemetry_metrics.num_unseen_messages.max().has_value());
  REQUIRE(metrics1.telemetry_metrics.num_unseen_messages.max().value() == 3);
  REQUIRE(metrics1.telemetry_metrics.num_unseen_messages.mean().has_value());
  REQUIRE(metrics1.telemetry_metrics.num_unseen_messages.mean().value() == Catch::Approx(3.0));
  REQUIRE(metrics1.telemetry_metrics.messages_dropped.min().has_value());
  REQUIRE(metrics1.telemetry_metrics.messages_dropped.min().value() == 1);
  REQUIRE(metrics1.telemetry_metrics.messages_dropped.max().has_value());
  REQUIRE(metrics1.telemetry_metrics.messages_dropped.max().value() == 1);
  REQUIRE(metrics1.telemetry_metrics.messages_dropped.mean().has_value());
  REQUIRE(metrics1.telemetry_metrics.messages_dropped.mean().value() == Catch::Approx(1.0));
  REQUIRE(metrics1.telemetry_metrics.message_staleness.min().has_value());
  REQUIRE(metrics1.telemetry_metrics.message_staleness.min().value().count() == 500);

  std::ignore = subscriber.commit(*input);

  // Publish another message.
  publish(TestMsg{.value = 3});

  // Make dial input again.
  input = subscriber.make_dial_input(new_msgs_max_limit, jewels::time::SyncTime(std::chrono::nanoseconds{30000}));
  REQUIRE(input);

  // Check metrics after second call.
  const auto& metrics2 = subscriber.get_aggregated_input_metrics();
  REQUIRE(metrics2.event_metrics.size() == 2);
  REQUIRE(metrics2.event_metrics[1].num_unseen_messages == 1);
  REQUIRE(metrics2.event_metrics[1].messages_dropped == 0);

  // Check telemetry aggregation.
  REQUIRE(metrics2.telemetry_metrics.num_unseen_messages.min().has_value());
  REQUIRE(metrics2.telemetry_metrics.num_unseen_messages.min().value() == 1);
  REQUIRE(metrics2.telemetry_metrics.num_unseen_messages.max().has_value());
  REQUIRE(metrics2.telemetry_metrics.num_unseen_messages.max().value() == 3);
  REQUIRE(metrics2.telemetry_metrics.num_unseen_messages.mean().has_value());
  REQUIRE(metrics2.telemetry_metrics.num_unseen_messages.mean().value() == Catch::Approx(2.0));
  REQUIRE(metrics2.telemetry_metrics.messages_dropped.min().has_value());
  REQUIRE(metrics2.telemetry_metrics.messages_dropped.min().value() == 0);
  REQUIRE(metrics2.telemetry_metrics.messages_dropped.max().has_value());
  REQUIRE(metrics2.telemetry_metrics.messages_dropped.max().value() == 1);
  REQUIRE(metrics2.telemetry_metrics.messages_dropped.mean().has_value());
  REQUIRE(metrics2.telemetry_metrics.messages_dropped.mean().value() == Catch::Approx(0.5));
  // Reset metrics.
  subscriber.reset_metrics();
  const auto& final_metrics = subscriber.get_aggregated_input_metrics();
  REQUIRE(final_metrics.event_metrics.empty());
  REQUIRE_FALSE(final_metrics.telemetry_metrics.num_unseen_messages.min().has_value());
  REQUIRE_FALSE(final_metrics.telemetry_metrics.message_staleness.min().has_value());
  REQUIRE_FALSE(final_metrics.telemetry_metrics.messages_dropped.min().has_value());
}

TEST_CASE("Not connected input view")
{
  auto subscriber = InputView<NoCopyPolicy>(10, jewels::memory::MemoryResource{std::pmr::new_delete_resource()}, false);
  auto input = subscriber.make_dial_input(1, jewels::time::SyncTime{});
  REQUIRE(input);
  REQUIRE_FALSE(input->connected());
}

} // namespace
} // namespace clockwork
