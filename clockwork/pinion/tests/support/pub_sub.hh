// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/pinion/abstract_channel.hh"
#include "clockwork/pinion/observer.hh"
#include "clockwork/pinion/publishable.hh"
#include "clockwork/pinion/publisher_slot_ref.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace clockwork::testing
{

/// A noop observer for test purposes.
class FakeObserver : public pinion::Observer
{
public:
  void notify(const Event& /*event*/) override {}
};

/// A simple observer that accumulates events for insepction in tests.
struct TestObserver : public pinion::Observer
{
  void notify(const Event& event_in) final
  {
    event = event_in;
    events.push_back(event_in);
  }
  void reset()
  {
    event.reset();
    events.clear();
  }
  std::optional<Event> event;
  std::vector<Event> events;
};

/// Publish a message.
/// @tparam T The message type.
/// @param publisher The publisher to publish to.
/// @param message The message to publish.
/// @param publish_time Timestamp to commit the message with.
template <typename T>
void publish(pinion::PublisherHandle& publisher, const T& message, const jewels::time::SyncTime publish_time)
{
  auto reserved = publisher.reserve();
  REQUIRE(reserved);
  auto publishable = pinion::Publishable<T>::try_make(jewels::memory::make_non_null_from_ref(*reserved));
  REQUIRE(publishable);
  publishable->message() = message;
  REQUIRE(reserved->commit(publish_time));
}

/// Publish a message.
/// @tparam T The message type.
/// @param publisher The publisher to publish to.
/// @param message The message to publish.
/// @param publish_time Message publish timestamp.
/// @param sequence_number Overrides for the message sequence number.
/// @param source_commit_time Message commit time from the original sender.
template <typename T>
void publish(
  pinion::PublisherHandle& publisher,
  const T& message,
  const jewels::time::SyncTime publish_time,
  uint64_t sequence_number,
  jewels::time::SyncTime source_commit_time)
{
  auto reserved = publisher.reserve();
  REQUIRE(reserved);
  auto publishable = pinion::Publishable<T>::try_make(jewels::memory::make_non_null_from_ref(*reserved));
  REQUIRE(publishable);
  publishable->message() = message;
  REQUIRE(reserved->commit(publish_time, sequence_number, source_commit_time));
}

/// Publish a message.
/// @tparam T The message type.
/// @param publisher The publisher to publish to.
/// @param message The message to publish.
template <typename T>
void publish(pinion::PublisherHandle& publisher, const T& message)
{
  publish(publisher, message, jewels::time::SyncClock::now());
}

/// Dump the message available on a subscriber handle.
/// @tparam T The message type.
/// @param subscriber The handle to dump from.
///@{
template <typename T>
std::vector<T> dump(const pinion::SubscriberHandle& subscriber)
{
  auto span = pinion::to_message_range<const T>(subscriber.available());
  REQUIRE(span);
  return std::vector<T>(span->begin(), span->end());
}
template <typename T>
std::vector<T> dump(const std::shared_ptr<pinion::AbstractChannel>& channel)
{
  REQUIRE(channel);
  auto span = pinion::to_message_range<const T>(channel->available());
  REQUIRE(span);
  return std::vector<T>(span->begin(), span->end());
}
///@}

/// Get the most recent available on a subscriber handle.
/// @tparam T The message type.
/// @param subscriber The handle to check.
///@{
template <typename T>
std::optional<T> last(const pinion::SubscriberHandle& subscriber)
{
  auto span = pinion::to_message_range<const T>(subscriber.available());
  REQUIRE(span);
  return (span->empty() ? std::optional<T>{} : span->back());
}
template <typename T>
std::optional<T> last(const std::shared_ptr<pinion::AbstractChannel>& channel)
{
  REQUIRE(channel);
  auto span = pinion::to_message_range<const T>(channel->available());
  REQUIRE(span);
  return (span->empty() ? std::optional<T>{} : span->back());
}
///@}

} // namespace clockwork::testing
