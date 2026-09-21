// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/abstract_channel.hh"
#include "clockwork/pinion/abstract_channel_factory.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/buffer_index.hh"
#include "clockwork/pinion/buffer_layout.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/observer.hh"
#include "clockwork/pinion/publishable.hh"
#include "clockwork/pinion/publisher_slot_ref.hh"
#include "clockwork/pinion/shm_channel.hh"
#include "clockwork/pinion/shm_channel_factory.hh"
#include "clockwork/pinion/shm_publisher.hh"
#include "clockwork/pinion/shm_subscriber.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/slot_ref.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "clockwork/pinion/tests/support/epoll_snooper.hh"
#include "clockwork/pinion/tests/support/pub_sub.hh"
#include "clockwork/pinion/tests/support/tmp_shm_namespace.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file.hh"
#include "jewels/filesystem/filesystem.hh"
#include "jewels/filesystem/mmap_region.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/default_memory_resource.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_tostring.hpp>

#include <array>
#include <chrono>
#include <climits>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <memory_resource>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <thread>
#include <tuple>
#include <unistd.h>
#include <utility>
#include <vector>

namespace clockwork::pinion
{
namespace
{
struct Tag
{
};

class ShmChannelOpenHack : public ShmChannel // NOLINT(fuchsia-multiple-inheritance) spurious error
{
public:
  static auto open_buffer(
    jewels::memory::MemoryResource memres,
    const jewels::filesystem::Directory& shm_dir,
    std::string_view channel_uuid_str,
    const pinion::BufferLayout& layout,
    Role role,
    ResumeBehavior resume_behavior)
  {
    return ShmChannel::open_buffer(memres, shm_dir, channel_uuid_str, layout, role, resume_behavior);
  }

protected:
  using ShmChannel::ShmChannel;
};

template <typename T, size_t num_slots>
void publish(PublisherHandle& publisher, std::vector<T>& expected, const T& message)
{
  testing::publish(publisher, message);
  expected.push_back(message);
  if (expected.size() > num_slots)
  {
    expected.erase(expected.begin(), expected.begin() + static_cast<ssize_t>(expected.size() - num_slots));
  }
}

/// Test observer
class TestObserver : public Observer
{
public:
  explicit TestObserver(std::function<void(const Event&)> callback);

  TestObserver(const TestObserver&) = default;
  TestObserver(TestObserver&&) = default;
  TestObserver& operator=(const TestObserver&) = default;
  TestObserver& operator=(TestObserver&&) = default;

  ~TestObserver() override = default;

  /// Notify the observer of a new message on a channel.
  /// @param event struct containing details of the event that triggered the notification
  void notify(const Event& event) override;

private:
  std::function<void(const Event&)> callback_;
};

TestObserver::TestObserver(std::function<void(const Event&)> callback)
  : callback_(std::move(callback))
{
}

void TestObserver::notify(const Event& event)
{
  callback_(event);
}

} // namespace

TEST_CASE("ShmChannel main")
{
  using testing::dump;
  using testing::TestObserver;
  using File = jewels::filesystem::File;
  using Directory = jewels::filesystem::Directory;
  using Msg = uint32_t;

  constexpr size_t num_slots = 17;
  constexpr size_t num_overwrite = 11;
  constexpr size_t message_size = sizeof(Msg);
  constexpr size_t max_observer = 4;
  constexpr size_t max_connections = 4;
  constexpr ShmChannel::ResumeBehavior resume_behavior = ShmChannel::ResumeBehavior::no_resume;

  const pinion::BufferLayout buffer_layout{
    .num_slots = num_slots,
    .message_size = message_size,
    .is_published_once = false,
  };

  const std::string channel_uuid_str = jewels::Uuid<Tag>::random_uuid().to_string();
  constexpr auto channel_name = "/test/channel";
  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};

  const support::TmpShmNamespace tmp_namespace;
  const auto& socket_ns = tmp_namespace.get_namespace();
  auto channel_root = Directory::create_open(Directory::at_cwd, tmp_namespace.get_full_path());
  REQUIRE(channel_root);

  SECTION("file tests")
  {
    auto open =
      [&memres, &channel_root](std::string_view channel_uuid_str, const BufferLayout& layout, ShmChannel::Role role)
    { return ShmChannelOpenHack::open_buffer(memres, *channel_root, channel_uuid_str, layout, role, resume_behavior); };

    SECTION("basic creation")
    {
      // Sanity check file doesn't exist
      CHECK(!File::open(channel_root->descriptor(), channel_uuid_str));

      { // Attempting to open the buffer file as a subscriber should fail
        auto result = open(channel_uuid_str, buffer_layout, ShmChannel::Role::subscriber);
        REQUIRE(!result);
        CHECK(result.error() == ShmChannel::Error::missing);
      }
      { // Opening as a publisher should succeed
        auto result = open(channel_uuid_str, buffer_layout, ShmChannel::Role::publisher);
        REQUIRE(result);
        CHECK(File::open(channel_root->descriptor(), channel_uuid_str));
      }
      // Opening as a subscriber should also be okay
      CHECK(open(channel_uuid_str, buffer_layout, ShmChannel::Role::subscriber));
      // Re-opening with a different layout shouldn't work for either role
      for (auto role : {ShmChannel::Role::publisher, ShmChannel::Role::subscriber})
      {
        const pinion::BufferLayout bad_layout{
          .num_slots = num_slots - 1,
          .message_size = message_size,
          .is_published_once = false,
        };
        auto result = open(channel_uuid_str, bad_layout, role);
        REQUIRE(!result);
        CHECK(result.error() == ShmChannel::Error::dirty);
      }
    }

    SECTION("non null terminated name")
    {
      const std::string backing_name = channel_uuid_str + "_trailing_bytes";
      const std::string_view file_name{backing_name.data(), channel_uuid_str.size()};

      auto publisher_result = open(file_name, buffer_layout, ShmChannel::Role::publisher);
      REQUIRE(publisher_result);
      CHECK(File::open(channel_root->descriptor(), channel_uuid_str));
      CHECK(!File::open(channel_root->descriptor(), backing_name));

      auto subscriber_result = open(file_name, buffer_layout, ShmChannel::Role::subscriber);
      REQUIRE(subscriber_result);
    }

    SECTION("too long name")
    {
      const std::string too_long_name(NAME_MAX + 1U, 'a');

      for (const auto role : {ShmChannel::Role::publisher, ShmChannel::Role::subscriber})
      {
        auto result = open(too_long_name, buffer_layout, role);
        REQUIRE(!result);
        CHECK(result.error() == ShmChannel::Error::fatal);
      }
    }

    SECTION("error - exists as link")
    {
      CHECK(::symlinkat("/proc/self/fd/0", channel_root->descriptor(), channel_uuid_str.c_str()) == 0);
      auto result = open(channel_uuid_str, buffer_layout, ShmChannel::Role::publisher);
      REQUIRE(!result);
      CHECK(result.error() == ShmChannel::Error::dirty);
    }

    SECTION("mmap io")
    {
      const std::array<char, 2 * message_size> ref{"123abc"};
      // Open the buffer and write to the mmap
      auto result = open(channel_uuid_str, buffer_layout, ShmChannel::Role::publisher);
      REQUIRE(result);
      auto bytes = std::get<0>(result.value()).to_span();
      REQUIRE(bytes.size() > ref.size());
      ::memcpy(bytes.data(), ref.data(), ref.size());
      // Flush
      CHECK(::msync(bytes.data(), ref.size(), MS_SYNC) == 0);
      // Read the file directly to confirm data was written
      auto file = File::open(channel_root->descriptor(), channel_uuid_str);
      REQUIRE(file);
      std::array<char, ref.size()> out{};
      auto read = file->pread_str(out);
      REQUIRE(read);
      CHECK(*read == ref.size());
      CHECK(std::string_view(ref.data(), ref.size()) == std::string_view(out.data(), out.size()));
    }
  }

  SECTION("comms tests")
  {
    // Create subscriber first, ensure that it fails with a "missing"
    {
      auto subscriber_channel = ShmSubscriber::open(
        memres,
        *channel_root,
        socket_ns,
        channel_uuid_str,
        channel_name,
        buffer_layout,
        max_observer,
        ShmSubscriber::SubscriberRole::subscriber,
        resume_behavior);
      REQUIRE(!subscriber_channel);
      CHECK(subscriber_channel.error() == ShmChannel::Error::missing);
    }

    EPollSnooper epoll;

    // Create publisher
    auto publisher_result = ShmPublisher::open(
      memres,
      *channel_root,
      socket_ns,
      channel_uuid_str,
      channel_name,
      buffer_layout,
      max_observer,
      max_connections,
      resume_behavior);
    REQUIRE(publisher_result);
    std::shared_ptr<ShmPublisher> publisher_channel = *std::move(publisher_result);
    REQUIRE(epoll.add(publisher_channel->socket(), EPOLLIN, publisher_channel));
    CHECK(publisher_channel->buffer()->layout().num_slots == buffer_layout.num_slots);
    CHECK(publisher_channel->buffer()->layout().message_size == buffer_layout.message_size);

    // Create a "local" subscriber from the IPC publisher
    auto subscriber1 = publisher_channel->make_subscriber();
    TestObserver subscriber1_events;
    CHECK(publisher_channel->add_observer(jewels::memory::make_non_null_from_ref(subscriber1_events)));

    // Extract the publisher (after the add_observer to ensure that is kept)
    auto publisher = publisher_channel->extract_publisher();
    CHECK_THROWS_AS(publisher_channel->publisher(), std::bad_optional_access);
    CHECK(!publisher_channel->extract_publisher());
    // PublisherHandle is now a proxy for the channel, so this will still succeed
    CHECK(publisher_channel->add_observer(jewels::memory::make_non_null_from_ref(subscriber1_events)));

    // Publish messages, enough to fill and overwrite the ring buffer
    std::vector<Msg> expected;
    for (uint32_t i = 0; i < num_slots + num_overwrite; i++)
    {
      publish<Msg, num_slots>(*publisher, expected, i);
    }

    // Check that the local subscriber reads the expected messages
    CHECK(dump<Msg>(subscriber1) == expected);
    // There are double the number of events since the observer was added twice
    CHECK(subscriber1_events.events.size() == 2 * (num_slots + num_overwrite));

    // Create IPC subscriber after writing data to sanity check that this will indeed load from the backing file
    {
      auto subscriber_result = ShmSubscriber::open(
        memres,
        *channel_root,
        socket_ns,
        channel_uuid_str,
        channel_name,
        buffer_layout,
        max_observer,
        ShmSubscriber::SubscriberRole::subscriber,
        resume_behavior);
      REQUIRE(subscriber_result);
      auto subscriber_channel = std::make_shared<ShmSubscriber>(*std::move(subscriber_result));
      REQUIRE(epoll.add(subscriber_channel->socket(), EPOLLIN, subscriber_channel));

      CHECK(publisher_channel->num_clients() == 0);
      epoll.notify(publisher_channel->socket(), EPOLLIN);
      CHECK(publisher_channel->num_clients() == 1);
      epoll.notify(publisher_channel->socket(), EPOLLIN);
      CHECK(publisher_channel->num_clients() == 1);

      auto subscriber2 = subscriber_channel->make_subscriber();
      CHECK(dump<Msg>(subscriber1) == expected);
      CHECK(dump<Msg>(subscriber2) == expected);

      TestObserver subscriber_notifications;
      REQUIRE(subscriber_channel->add_observer(jewels::memory::make_non_null_from_ref(subscriber_notifications)));

      // With the IPC subscriber created, publish more
      constexpr size_t more_publishes = num_slots + 1;
      for (uint32_t i = 0; i < more_publishes; i++)
      {
        publish<Msg, num_slots>(*publisher, expected, i);
      }
      CHECK(dump<Msg>(subscriber1) == expected);
      CHECK(dump<Msg>(subscriber2) == expected);

      // Trigger to check notifications were generated
      CHECK(subscriber_notifications.events.empty());
      epoll.notify(subscriber_channel->socket(), EPOLLIN);
      CHECK(subscriber_notifications.events.size() == more_publishes + 1U);
      epoll.remove(subscriber_channel->socket());
    }

    // With the subscriber destructed, do a publish in order to flush connections
    // (the publish call will discard any subscribers that it finds to be disconnected while attempting to send a
    // notification message)
    CHECK(publisher_channel->num_clients() == 1);
    publish<Msg, num_slots>(*publisher, expected, 200);
    CHECK(publisher_channel->num_clients() == 0);
  }

  SECTION("publisher disconnect")
  {
    EPollSnooper epoll;

    // Create publisher
    auto publisher_channel = ShmPublisher::open(
      memres,
      *channel_root,
      socket_ns,
      channel_uuid_str,
      channel_name,
      buffer_layout,
      max_observer,
      max_connections,
      resume_behavior);
    REQUIRE(publisher_channel);
    CHECK(publisher_channel.value()->buffer()->layout().num_slots == buffer_layout.num_slots);
    CHECK(publisher_channel.value()->buffer()->layout().message_size == buffer_layout.message_size);

    // Create subscriber
    auto subscriber_result = ShmSubscriber::open(
      memres,
      *channel_root,
      socket_ns,
      channel_uuid_str,
      channel_name,
      buffer_layout,
      max_observer,
      ShmSubscriber::SubscriberRole::subscriber,
      resume_behavior);
    REQUIRE(subscriber_result);
    auto subscriber_channel = std::make_shared<ShmSubscriber>(*std::move(subscriber_result));
    REQUIRE(subscriber_channel->socket() != -1);
    REQUIRE(epoll.add(subscriber_channel->socket(), EPOLLIN, subscriber_channel));

    // If the publisher closes, the subscriber should too.
    publisher_channel.value()->close_socket();
    epoll.notify(subscriber_channel->socket(), EPOLLIN);
    CHECK(subscriber_channel->socket() == -1);
  }

  SECTION("factory")
  {
    ShmChannelFactory factory(memres, std::move(*channel_root), tmp_namespace.get_namespace(), resume_behavior);
    auto opener = [&factory, &channel_uuid_str, &channel_name, &buffer_layout, &max_observer](auto role)
    { return factory.open(role, channel_uuid_str, channel_name, buffer_layout, max_observer); };

    auto shm_publisher = opener(ShmChannel::Role::publisher);
    auto shm_subscriber = opener(ShmChannel::Role::subscriber);
    REQUIRE(shm_publisher);
    REQUIRE(shm_subscriber);

    auto* shm_publisher_ptr = dynamic_cast<ShmPublisher*>(shm_publisher->get());
    REQUIRE(shm_publisher_ptr);

    auto& publisher = shm_publisher_ptr->publisher();
    auto subscriber = dynamic_cast<ShmSubscriber&>(**shm_subscriber).make_subscriber();

    std::vector<Msg> expected;
    publish<Msg, num_slots>(publisher, expected, 1);
    publish<Msg, num_slots>(publisher, expected, 2);
    CHECK(dump<Msg>(subscriber) == expected);
  }

  SECTION("factory + namespace")
  {
    using Role = ShmChannel::Role;

    auto ns0_factory = ShmChannelFactory::make(memres);
    CHECK(ns0_factory->socket_ns() == "/clockwork/pinion/pub");

    jewels::filesystem::Filesystem filesystem{jewels::memory::get_default_memory_resource()};

    const support::TmpShmNamespace ns1;
    auto ns1_factory = ShmChannelFactory::make(memres, ns1.get_namespace(), ns1.get_full_path());
    REQUIRE(ns1_factory);
    auto ns1_publisher =
      ns1_factory->open_shm_publisher(channel_uuid_str, channel_name, buffer_layout, max_observer).value();
    auto ns1_subscriber =
      ns1_factory->open_shm_subscriber(channel_uuid_str, channel_name, buffer_layout, max_observer).value();
    CHECK(ns1_factory->socket_ns() == std::pmr::string("/clockwork/" + ns1.get_namespace() + "/pinion/pub"));
    CHECK(filesystem.exists(ns1.get_full_path() / "clockwork" / ns1.get_namespace() / "pinion/pub" / channel_uuid_str));

    const support::TmpShmNamespace ns2;
    auto ns2_factory = ShmChannelFactory::make(memres, ns2.get_namespace(), ns2.get_full_path());
    REQUIRE(ns2_factory);
    auto ns2_publisher =
      ns2_factory->open(Role::publisher, channel_uuid_str, channel_name, buffer_layout, max_observer).value();
    auto ns2_subscriber =
      ns2_factory->open(Role::subscriber, channel_uuid_str, channel_name, buffer_layout, max_observer).value();
    CHECK(ns2_factory->socket_ns() == std::pmr::string("/clockwork/" + ns2.get_namespace() + "/pinion/pub"));
    CHECK(filesystem.exists(ns2.get_full_path() / "clockwork" / ns2.get_namespace() / "pinion/pub" / channel_uuid_str));
    TestObserver ns1_events;
    CHECK(ns1_publisher->add_observer(jewels::memory::make_non_null_from_ref(ns1_events)));
    TestObserver ns2_events;
    CHECK(ns2_publisher->add_observer(jewels::memory::make_non_null_from_ref(ns2_events)));

    std::vector<Msg> ns1_expected;
    publish<Msg, num_slots>(dynamic_cast<ShmPublisher&>(*ns1_publisher).publisher(), ns1_expected, 100);
    std::vector<Msg> ns2_expected;
    publish<Msg, num_slots>(dynamic_cast<ShmPublisher&>(*ns2_publisher).publisher(), ns2_expected, 100);
    publish<Msg, num_slots>(dynamic_cast<ShmPublisher&>(*ns2_publisher).publisher(), ns2_expected, 101);

    CHECK(dump<Msg>(dynamic_cast<ShmSubscriber&>(*ns1_subscriber).make_subscriber()) == ns1_expected);
    CHECK(dump<Msg>(dynamic_cast<ShmSubscriber&>(*ns2_subscriber).make_subscriber()) == ns2_expected);
    REQUIRE(ns1_events.events.size() == 1);
    REQUIRE(ns2_events.events.size() == 2);
  }
}

TEST_CASE("Factory registry")
{
  CHECK(pinion::ChannelFactoryContext::get() == nullptr);
  {
    const auto tmpns1 = pinion::support::TmpShmNamespace();
    auto factory1 = tmpns1.make_factory();
    const auto context1 = pinion::ChannelFactoryContext(factory1);
    CHECK(pinion::ChannelFactoryContext::get() == &factory1);
    {
      const auto tmpns2 = pinion::support::TmpShmNamespace();
      auto factory2 = tmpns2.make_factory();
      const auto context2 = pinion::ChannelFactoryContext(factory2);
      CHECK(pinion::ChannelFactoryContext::get() == &factory2);
    }
    CHECK(pinion::ChannelFactoryContext::get() == &factory1);
  }
  CHECK(pinion::ChannelFactoryContext::get() == nullptr);
}

TEST_CASE("Reconnect when resume is allowed")
{
  constexpr BufferLayout layout{
    .num_slots = 2UL,
    .message_size = sizeof(uint64_t),
    .is_published_once = false,
  };

  constexpr jewels::time::SyncTime fake_send_timestamp{std::chrono::seconds(12345)};

  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  const jewels::testing::TmpDirectoryGuard shm_dir;
  const auto socket_ns = jewels::Uuid<void>::random_uuid().to_string();
  const auto channel_uuid_str = jewels::Uuid<void>::random_uuid().to_string();
  constexpr auto channel_name = "/test/channel";

  EPollSnooper epoll;

  auto factory_result =
    ShmChannelFactory::make(memres, socket_ns, shm_dir.get_path().string(), ShmChannel::ResumeBehavior::dirty_resume);
  REQUIRE(factory_result);

  auto publisher_result = factory_result->open_shm_publisher(channel_uuid_str, channel_name, layout, 1U);
  REQUIRE(publisher_result);
  auto publisher = *std::move(publisher_result);
  REQUIRE(epoll.add(publisher->socket(), EPOLLIN, publisher));

  auto subscriber_result = factory_result->open_shm_subscriber(channel_uuid_str, channel_name, layout, 1U);
  REQUIRE(subscriber_result);
  const auto subscriber = *std::move(subscriber_result);
  auto subscriber_handle = subscriber->make_subscriber();
  REQUIRE(epoll.add(subscriber->socket(), EPOLLIN, subscriber));

  REQUIRE(subscriber->is_connected());
  REQUIRE_FALSE(subscriber->socket() == -1);
  epoll.notify(publisher->socket(), EPOLLIN);
  REQUIRE(publisher->num_clients() == 1U);

  auto notify_count = 0U;
  TestObserver observer{[&notify_count](const Observer::Event&) { ++notify_count; }};
  REQUIRE(subscriber->add_observer(jewels::memory::make_non_null_from_ref(observer)));

  epoll.notify(subscriber->socket(), EPOLLIN);
  REQUIRE(notify_count == 1U);

  uint64_t message1 = 1234U;
  {
    auto reservation = publisher->publisher().reserve();
    REQUIRE(reservation);
    std::memcpy(reservation->slots().front().message().data(), &message1, sizeof(uint64_t));
    REQUIRE(reservation->commit(fake_send_timestamp));
  }
  epoll.notify(subscriber->socket(), EPOLLIN);
  REQUIRE(notify_count == 2U);

  epoll.remove(publisher->socket());
  publisher.reset();
  epoll.notify(subscriber->socket(), EPOLLIN);
  REQUIRE(notify_count == 2U);
  REQUIRE_FALSE(subscriber->is_connected());
  REQUIRE(subscriber->socket() == -1);
  REQUIRE_FALSE(subscriber->timer_descriptor() == -1);

  auto available_slots = subscriber_handle.available();
  REQUIRE(available_slots.size() == 1U);
  REQUIRE(std::memcmp(available_slots[0].message().data(), &message1, sizeof(uint64_t)) == 0);

  epoll.notify(subscriber->timer_descriptor(), EPOLLIN);
  REQUIRE_FALSE(subscriber->is_connected());
  REQUIRE(subscriber->socket() == -1);
  REQUIRE_FALSE(subscriber->timer_descriptor() == -1);

  publisher_result = factory_result->open_shm_publisher(channel_uuid_str, channel_name, layout, 1U);
  REQUIRE(publisher_result);
  publisher = *std::move(publisher_result);
  REQUIRE(epoll.add(publisher->socket(), EPOLLIN, publisher));

  std::this_thread::sleep_for(std::chrono::seconds(2));
  epoll.notify(subscriber->timer_descriptor(), EPOLLIN);
  REQUIRE(subscriber->is_connected());
  REQUIRE_FALSE(subscriber->socket() == -1);
  REQUIRE(subscriber->timer_descriptor() == -1);
  REQUIRE(notify_count == 3U);

  REQUIRE(subscriber->is_connected());
  REQUIRE(publisher->num_clients() == 0U);
  epoll.notify(publisher->socket(), EPOLLIN);
  REQUIRE(publisher->num_clients() == 1U);

  uint64_t message2 = 2345U;
  {
    auto reservation = publisher->publisher().reserve();
    REQUIRE(reservation);
    std::memcpy(reservation->slots().front().message().data(), &message2, sizeof(uint64_t));
    REQUIRE(reservation->commit(fake_send_timestamp));
  }
  epoll.notify(subscriber->socket(), EPOLLIN);
  REQUIRE(notify_count == 5U);

  available_slots = subscriber_handle.available();
  REQUIRE(available_slots.size() == 2U);
  REQUIRE(std::memcmp(available_slots[0].message().data(), &message1, sizeof(uint64_t)) == 0);
  REQUIRE(std::memcmp(available_slots[1].message().data(), &message2, sizeof(uint64_t)) == 0);
}
TEST_CASE("Reconnect fails when resume is disallowed")
{
  constexpr BufferLayout layout{
    .num_slots = 2UL,
    .message_size = sizeof(uint64_t),
    .is_published_once = false,
  };
  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  const jewels::testing::TmpDirectoryGuard shm_dir;
  const auto socket_ns = jewels::Uuid<void>::random_uuid().to_string();
  const auto channel_uuid_str = jewels::Uuid<void>::random_uuid().to_string();
  constexpr auto channel_name = "/test/channel";

  auto factory_result =
    ShmChannelFactory::make(memres, socket_ns, shm_dir.get_path().string(), ShmChannel::ResumeBehavior::no_resume);
  auto publisher_result = factory_result->open_shm_publisher(channel_uuid_str, channel_name, layout, 1U);
  REQUIRE(publisher_result);
  auto publisher = *std::move(publisher_result);
  publisher.reset();
  publisher_result = factory_result->open_shm_publisher(channel_uuid_str, channel_name, layout, 1U);
  REQUIRE(publisher_result.error() == ShmChannel::Error::dirty);
}

TEST_CASE("Publisher handle") // NOLINT(readability-function-size) This test is a bit long due to multiple sections
{
  constexpr auto fake_publish_time{jewels::time::SyncTime{std::chrono::nanoseconds{12345}}};
  using testing::TestObserver;

  constexpr BufferLayout layout{
    .num_slots = 2UL,
    .message_size = 8UL,
    .is_published_once = false,
  };

  const support::TmpShmNamespace tmp_namespace;
  auto channel_factory = tmp_namespace.make_factory();

  auto publisher_result =
    channel_factory.open_shm_publisher(jewels::Uuid<Tag>::random_uuid().to_string(), "ch1", layout, 2);
  REQUIRE(publisher_result);
  auto pub_handle_result = publisher_result.value()->extract_publisher();
  REQUIRE(pub_handle_result);
  PublisherHandle pub_handle = *std::move(pub_handle_result);
  const AbstractChannel& sub_handle = **publisher_result;
  Buffer& buffer = *publisher_result.value()->buffer();

  SECTION("Check state transitions")
  {
    auto reservation = pub_handle.reserve();
    REQUIRE(reservation);
    auto slot_ref = reservation->slots().begin();
    REQUIRE(slot_ref.state() == ReservationState::discard);
    slot_ref.mark_for_commit();
    REQUIRE(slot_ref.state() == ReservationState::commit);
    slot_ref.mark_for_discard();
    REQUIRE(slot_ref.state() == ReservationState::discard);
  }

  SECTION("Move constructor")
  {
    auto reservation = pub_handle.reserve();
    REQUIRE(reservation);
    auto slot_ref = reservation->slots().begin();
    SECTION("Move with discard")
    {
      slot_ref.mark_for_discard();
      auto moved_to{std::move(*reservation)};
      REQUIRE(slot_ref.state() == ReservationState::discard);
    }
    SECTION("Move with commit")
    {
      slot_ref.mark_for_commit();
      auto moved_to{std::move(*reservation)};
      REQUIRE(slot_ref.state() == ReservationState::commit);
      REQUIRE(reservation->discard());
      REQUIRE(moved_to.discard());
    }
    SECTION("Moved from")
    {
      REQUIRE(reservation->pending());
      auto moved_to{std::move(*reservation)};
      REQUIRE(slot_ref.state() == ReservationState::discard);
      REQUIRE_FALSE(reservation->pending());
    }
  }

  SECTION("Destructor")
  {
    TestObserver observer;
    REQUIRE(pub_handle.add_observer(jewels::memory::make_non_null_from_ref(observer)));

    auto reservation = pub_handle.reserve();
    REQUIRE(reservation);
    auto slot_ref = reservation->slots().begin();

    SECTION("Commit is ignored in destructor")
    {
      slot_ref.mark_for_commit();
      REQUIRE_NOTHROW([&] { auto copied{*std::move(reservation)}; }());
      CHECK(!observer.event);
    }
  }

  SECTION("Duplicate reservation")
  {
    auto reservation = pub_handle.reserve();
    REQUIRE(reservation);
    REQUIRE(pub_handle.reserve() == jewels::unexpected{ReserveError::existing_reservation});
  }

  SECTION("Corrupted head")
  {
    auto reservation = pub_handle.reserve();
    REQUIRE(reservation);
    REQUIRE(buffer.increment_head(BufferIndex{0UL}, 1UL) == BufferIndex{1UL});
    REQUIRE(reservation->commit(fake_publish_time) == jewels::unexpected{WriteError::unexpected_reservation});
    REQUIRE_THROWS_AS(reservation->~PublisherReservation(), std::runtime_error);
  }

  SECTION("Commit")
  {
    SECTION("Mark then process")
    {
      auto reservation = pub_handle.reserve();
      REQUIRE(reservation);
      reservation->slots().begin().mark_for_commit();
      REQUIRE(buffer.tail() == 0UL);
      REQUIRE(buffer.head() == 0UL);
      REQUIRE(reservation->process(fake_publish_time));
      REQUIRE(buffer.tail() == 0UL);
      REQUIRE(buffer.head() == 1UL);
    }
    SECTION("Force commit")
    {
      auto reservation = pub_handle.reserve();
      REQUIRE(reservation);
      REQUIRE(buffer.tail() == 0UL);
      REQUIRE(buffer.head() == 0UL);
      REQUIRE(reservation->commit(fake_publish_time));
      REQUIRE(buffer.tail() == 0UL);
      REQUIRE(buffer.head() == 1UL);
    }
    SECTION("Header")
    {
      auto reservation0 = pub_handle.reserve();
      REQUIRE(reservation0);
      auto slot_ref0 = reservation0->slots().begin();
      REQUIRE(slot_ref0->header()->sequence_number == PublisherReservation::sequence_number_discard);
      REQUIRE(slot_ref0->header()->publish_timestamp == PublisherReservation::unset_publish_timestamp);
      REQUIRE(slot_ref0->header()->source_commit_timestamp == 0L);
      REQUIRE(slot_ref0->header()->latest_commit_timestamp == 0L);
      const auto commit_time0 = jewels::time::SyncClock::now();
      REQUIRE(reservation0->commit(fake_publish_time));
      REQUIRE(slot_ref0->header()->sequence_number == 0UL);
      REQUIRE(slot_ref0->header()->publish_timestamp == fake_publish_time.time_since_epoch().count());
      REQUIRE(slot_ref0->header()->source_commit_timestamp >= commit_time0.time_since_epoch().count());
      REQUIRE(slot_ref0->header()->latest_commit_timestamp == slot_ref0->header()->source_commit_timestamp);

      auto reservation1 = pub_handle.reserve();
      REQUIRE(reservation1);
      auto slot_ref1 = reservation1->slots().begin();
      REQUIRE(slot_ref1->header()->sequence_number == PublisherReservation::sequence_number_discard);
      REQUIRE(slot_ref1->header()->publish_timestamp == PublisherReservation::unset_publish_timestamp);
      REQUIRE(slot_ref1->header()->source_commit_timestamp == 0L);
      REQUIRE(slot_ref1->header()->latest_commit_timestamp == 0L);
      const auto commit_time1 = jewels::time::SyncClock::now();
      REQUIRE(reservation1->commit(fake_publish_time + std::chrono::nanoseconds{1}));
      REQUIRE(slot_ref1->header()->sequence_number == 1UL);
      REQUIRE(slot_ref1->header()->publish_timestamp == fake_publish_time.time_since_epoch().count() + 1L);
      REQUIRE(slot_ref1->header()->source_commit_timestamp >= commit_time1.time_since_epoch().count());
      REQUIRE(slot_ref1->header()->latest_commit_timestamp == slot_ref1->header()->source_commit_timestamp);
    }
    SECTION("Header with sequence number and source commit time")
    {
      constexpr uint64_t fake_sequence_number1 = 123456U;
      constexpr auto fake_commit_time1{jewels::time::SyncTime{std::chrono::nanoseconds{23456}}};
      constexpr uint64_t fake_sequence_number2 = 234567U;
      constexpr auto fake_commit_time2{jewels::time::SyncTime{std::chrono::nanoseconds{34578}}};
      auto reservation0 = pub_handle.reserve();
      REQUIRE(reservation0);
      auto slot_ref0 = reservation0->slots().begin();
      REQUIRE(slot_ref0->header()->sequence_number == PublisherReservation::sequence_number_discard);
      REQUIRE(slot_ref0->header()->publish_timestamp == PublisherReservation::unset_publish_timestamp);
      REQUIRE(slot_ref0->header()->source_commit_timestamp == 0L);
      REQUIRE(slot_ref0->header()->latest_commit_timestamp == 0L);
      const auto commit_time0 = jewels::time::SyncClock::now();
      REQUIRE(reservation0->commit(fake_publish_time, fake_sequence_number1, fake_commit_time1));
      REQUIRE(slot_ref0->header()->sequence_number == fake_sequence_number1);
      REQUIRE(slot_ref0->header()->publish_timestamp == fake_publish_time.time_since_epoch().count());
      REQUIRE(slot_ref0->header()->source_commit_timestamp == fake_commit_time1.time_since_epoch().count());
      REQUIRE(slot_ref0->header()->latest_commit_timestamp >= commit_time0.time_since_epoch().count());

      auto reservation1 = pub_handle.reserve();
      REQUIRE(reservation1);
      auto slot_ref1 = reservation1->slots().begin();
      REQUIRE(slot_ref1->header()->sequence_number == PublisherReservation::sequence_number_discard);
      REQUIRE(slot_ref1->header()->publish_timestamp == PublisherReservation::unset_publish_timestamp);
      REQUIRE(slot_ref1->header()->source_commit_timestamp == 0L);
      REQUIRE(slot_ref1->header()->latest_commit_timestamp == 0L);
      const auto commit_time1 = jewels::time::SyncClock::now();
      REQUIRE(reservation1->commit(
        fake_publish_time + std::chrono::nanoseconds{1}, fake_sequence_number2, fake_commit_time2));
      REQUIRE(slot_ref1->header()->sequence_number == fake_sequence_number2);
      REQUIRE(slot_ref1->header()->publish_timestamp == fake_publish_time.time_since_epoch().count() + 1L);
      REQUIRE(slot_ref1->header()->source_commit_timestamp == fake_commit_time2.time_since_epoch().count());
      REQUIRE(slot_ref1->header()->latest_commit_timestamp >= commit_time1.time_since_epoch().count());
    }
    SECTION("Twice fails")
    {
      auto reservation = pub_handle.reserve();
      REQUIRE(reservation);
      REQUIRE(reservation->commit(fake_publish_time));
      REQUIRE_FALSE(reservation->commit(fake_publish_time) == jewels::unexpected{WriteError::unexpected_head});
    }
    SECTION("After discard fails")
    {
      auto reservation = pub_handle.reserve();
      REQUIRE(reservation);
      REQUIRE(reservation->discard());
      REQUIRE_FALSE(reservation->commit(fake_publish_time) == jewels::unexpected{WriteError::unexpected_head});
    }
  }

  SECTION("Discard")
  {
    SECTION("Mark then process")
    {
      auto reservation = pub_handle.reserve();
      REQUIRE(reservation);
      reservation->slots().begin().mark_for_discard();
      REQUIRE(buffer.tail() == 0UL);
      REQUIRE(buffer.head() == 0UL);
      REQUIRE(reservation->process(fake_publish_time));
      REQUIRE(buffer.tail() == 0UL);
      REQUIRE(buffer.head() == 0UL);
    }
    SECTION("Force discard")
    {
      auto reservation = pub_handle.reserve();
      REQUIRE(reservation);
      REQUIRE(buffer.tail() == 0UL);
      REQUIRE(buffer.head() == 0UL);
      REQUIRE(reservation->discard());
      REQUIRE(buffer.tail() == 0UL);
      REQUIRE(buffer.head() == 0UL);
    }
    SECTION("Twice fails")
    {
      auto reservation = pub_handle.reserve();
      REQUIRE(reservation);
      REQUIRE(reservation->discard());
      REQUIRE_FALSE(reservation->discard() == jewels::unexpected{WriteError::unexpected_head});
    }
    SECTION("After commit fails")
    {
      auto reservation = pub_handle.reserve();
      REQUIRE(reservation);
      REQUIRE(reservation->commit(fake_publish_time));
      REQUIRE_FALSE(reservation->discard() == jewels::unexpected{WriteError::unexpected_head});
    }
  }

  SECTION("Discard-commit sequencing")
  {
    // Reserve and discard.  Has capacity.
    {
      auto reservation = pub_handle.reserve();
      REQUIRE(reservation);
      REQUIRE(buffer.tail() == 0UL);
      REQUIRE(buffer.head() == 0UL);
      REQUIRE(reservation->slots().begin()->header()->sequence_number == PublisherReservation::sequence_number_discard);
      REQUIRE(reservation->discard());
    }

    // Buffer has not changed.
    REQUIRE(std::ranges::empty(sub_handle.available()));
    REQUIRE(buffer.tail() == 0UL);
    REQUIRE(buffer.head() == 0UL);

    // Reserve and commit
    {
      auto reservation = pub_handle.reserve();
      REQUIRE(reservation);
      REQUIRE(buffer.tail() == 0UL);
      REQUIRE(buffer.head() == 0UL);
      REQUIRE(reservation->slots().begin()->header()->sequence_number == PublisherReservation::sequence_number_discard);
      REQUIRE(reservation->commit(fake_publish_time));
    }

    // Commit changed the buffer.
    REQUIRE(std::ranges::size(sub_handle.available()) == 1UL);
    REQUIRE(buffer.tail() == 0UL);
    REQUIRE(sub_handle.available().front().header()->sequence_number == 0UL);

    // Reserve and discard.  Has capacity.
    {
      auto reservation = pub_handle.reserve();
      REQUIRE(reservation);
      REQUIRE(buffer.tail() == 0UL);
      REQUIRE(buffer.head() == 1UL);
      REQUIRE(reservation->slots().begin()->header()->sequence_number == PublisherReservation::sequence_number_discard);
      REQUIRE(reservation->discard());
    }

    // Buffer has not changed.
    REQUIRE(std::ranges::size(sub_handle.available()) == 1UL);
    REQUIRE(buffer.tail() == 0UL);
    REQUIRE(buffer.head() == 1UL);
    REQUIRE(sub_handle.available().front().header()->sequence_number == 0UL);

    // Reserve and commit.  Now full.
    {
      auto reservation = pub_handle.reserve();
      REQUIRE(reservation);
      REQUIRE(buffer.tail() == 0UL);
      REQUIRE(buffer.head() == 1UL);
      REQUIRE(reservation->slots().begin()->header()->sequence_number == PublisherReservation::sequence_number_discard);
      REQUIRE(reservation->commit(fake_publish_time));
    }

    // Commit changed the buffer.
    REQUIRE(std::ranges::size(sub_handle.available()) == 2UL);
    REQUIRE(buffer.tail() == 0UL);
    REQUIRE(buffer.head() == 2UL);
    REQUIRE(sub_handle.available().front().header()->sequence_number == 0UL);
    REQUIRE(sub_handle.available().back().header()->sequence_number == 1UL);

    // Reserve and discard. No capacity.
    {
      auto reservation = pub_handle.reserve();
      REQUIRE(reservation);
      REQUIRE(buffer.tail() == 1UL);
      REQUIRE(buffer.head() == 2UL);
      REQUIRE(reservation->slots().begin()->header()->sequence_number == PublisherReservation::sequence_number_discard);
      REQUIRE(std::ranges::size(sub_handle.available()) == 1UL);
      REQUIRE(sub_handle.available().front().header()->sequence_number == 1UL);
      REQUIRE(reservation->discard());
    }

    // Discard changed the buffer.  Oldest element remains hidden.
    REQUIRE(std::ranges::size(sub_handle.available()) == 1UL);
    REQUIRE(buffer.tail() == 1UL);
    REQUIRE(buffer.head() == 2UL);
    REQUIRE(sub_handle.available().front().header()->sequence_number == 1UL);

    // Reserve and commit. Back to full.
    {
      auto reservation = pub_handle.reserve();
      REQUIRE(reservation);
      REQUIRE(buffer.tail() == 1UL);
      REQUIRE(buffer.head() == 2UL);
      REQUIRE(reservation->slots().begin()->header()->sequence_number == PublisherReservation::sequence_number_discard);
      REQUIRE(std::ranges::size(sub_handle.available()) == 1UL);
      REQUIRE(sub_handle.available().front().header()->sequence_number == 1UL);
      REQUIRE(reservation->commit(fake_publish_time));
    }

    // Commit changed the buffer.
    REQUIRE(std::ranges::size(sub_handle.available()) == 2UL);
    REQUIRE(buffer.tail() == 1UL);
    REQUIRE(buffer.head() == 3UL);
    REQUIRE(sub_handle.available().front().header()->sequence_number == 1UL);
    REQUIRE(sub_handle.available().back().header()->sequence_number == 2UL);

    // Reserve and commit. Stays full.
    {
      auto reservation = pub_handle.reserve();
      REQUIRE(reservation);
      REQUIRE(buffer.tail() == 2UL);
      REQUIRE(buffer.head() == 3UL);
      REQUIRE(reservation->slots().begin()->header()->sequence_number == PublisherReservation::sequence_number_discard);
      REQUIRE(std::ranges::size(sub_handle.available()) == 1UL);
      REQUIRE(sub_handle.available().front().header()->sequence_number == 2UL);
      REQUIRE(reservation->commit(fake_publish_time));
    }

    // Commit changed the buffer.
    REQUIRE(std::ranges::size(sub_handle.available()) == 2UL);
    REQUIRE(buffer.tail() == 2UL);
    REQUIRE(buffer.head() == 4UL);
    REQUIRE(sub_handle.available().front().header()->sequence_number == 2UL);
    REQUIRE(sub_handle.available().back().header()->sequence_number == 3UL);
  }

  SECTION("Observers")
  {
    SECTION("Too many observers")
    {
      TestObserver observer;
      REQUIRE(pub_handle.add_observer(jewels::memory::make_non_null_from_ref(observer)));
      REQUIRE(pub_handle.add_observer(jewels::memory::make_non_null_from_ref(observer)));
      REQUIRE_FALSE(pub_handle.add_observer(jewels::memory::make_non_null_from_ref(observer)));
    }
    SECTION("One observer")
    {
      TestObserver observer;
      REQUIRE(pub_handle.add_observer(jewels::memory::make_non_null_from_ref(observer)));
      for (int i = 0; i < 3; i++)
      {
        observer.reset();
        auto reservation = pub_handle.reserve();
        REQUIRE(reservation);
        REQUIRE_FALSE(observer.event);
        REQUIRE(reservation->commit(fake_publish_time));
        REQUIRE(observer.event);
      }
    }
    SECTION("Two observers")
    {
      TestObserver observer_a;
      TestObserver observer_b;
      REQUIRE(pub_handle.add_observer(jewels::memory::make_non_null_from_ref(observer_a)));
      REQUIRE(pub_handle.add_observer(jewels::memory::make_non_null_from_ref(observer_b)));
      auto reservation = pub_handle.reserve();
      REQUIRE(reservation);
      REQUIRE_FALSE(observer_a.event);
      REQUIRE_FALSE(observer_b.event);
      REQUIRE(reservation->commit(fake_publish_time));
      REQUIRE(observer_a.event);
      REQUIRE(observer_b.event);
    }
  }

  SECTION("Publishable")
  {
    auto reservation = pub_handle.reserve();
    REQUIRE(reservation);

    SECTION("Size mismatch")
    {
      REQUIRE_FALSE(Publishable<uint32_t>::try_make(reservation->slots().begin()));
      REQUIRE_FALSE(Publishable<std::pair<uint64_t, uint64_t>>::try_make(reservation->slots().begin()));
    }
    SECTION("Valid size")
    {
      auto publishable = Publishable<uint64_t>::try_make(reservation->slots().begin());
      REQUIRE(publishable);
      REQUIRE(reservation->slots().begin().state() == ReservationState::discard);
      publishable->message() = 123456789UL;
      SECTION("Publish")
      {
        publishable->mark_for_publish();
        CHECK(publishable->get_metrics_publish_count() == 1U);
        CHECK_FALSE(publishable->get_metrics_first_sequence_number());
        REQUIRE(reservation->slots().begin().state() == ReservationState::commit);
        REQUIRE(std::ranges::empty(sub_handle.available()));
        REQUIRE(reservation->process(fake_publish_time));
        auto message_range = to_message_range<const uint64_t>(sub_handle.available());
        REQUIRE(message_range);
        REQUIRE(std::ranges::size(*message_range) == 1UL);
        REQUIRE(message_range->front() == 123456789UL);
      }
      SECTION("Sim only mark for publish")
      {
        constexpr jewels::time::SyncTime sim_publish_time{std::chrono::nanoseconds{1234567890}};
        publishable->sim_only_mark_for_publish_with_fake_timestamp(sim_publish_time);
        REQUIRE(reservation->slots().begin().state() == ReservationState::commit);
        REQUIRE(std::ranges::empty(sub_handle.available()));
        REQUIRE(reservation->process(fake_publish_time));
        REQUIRE(!std::ranges::empty(sub_handle.available()));
        auto begin = sub_handle.available().begin();
        REQUIRE(begin->header()->publish_timestamp == sim_publish_time.time_since_epoch().count());
      }
    }
  }

  SECTION("Multi-message indexed access checks bounds")
  {
    constexpr auto batch_size = 2UL;
    auto batch_reservation = pub_handle.reserve(batch_size);
    REQUIRE(batch_reservation);
    auto publishable = Publishable<uint64_t, batch_size>::try_make(batch_reservation->slots());
    REQUIRE(publishable);
    REQUIRE_NOTHROW(static_cast<void>(publishable->message(batch_size - 1UL)));
    REQUIRE_NOTHROW(static_cast<void>(publishable->device_ptr(batch_size - 1UL)));
    REQUIRE_THROWS_AS(static_cast<void>(publishable->message(batch_size)), std::out_of_range);
    REQUIRE_THROWS_AS(static_cast<void>(publishable->device_ptr(batch_size)), std::out_of_range);
  }
}

TEST_CASE("Processing of multiple publisher reservations")
{
  constexpr auto fake_publish_time{jewels::time::SyncTime{std::chrono::nanoseconds{12345}}};
  constexpr BufferLayout layout{
    .num_slots = 2UL,
    .message_size = 8UL,
    .is_published_once = false,
  };

  const support::TmpShmNamespace tmp_namespace;
  auto channel_factory = tmp_namespace.make_factory();

  auto publisher0_result =
    channel_factory.open_shm_publisher(jewels::Uuid<Tag>::random_uuid().to_string(), "ch0", layout, 2);
  REQUIRE(publisher0_result);
  auto pub_handle0_result = publisher0_result.value()->extract_publisher();
  REQUIRE(pub_handle0_result);
  PublisherHandle pub_handle0 = *std::move(pub_handle0_result);
  Buffer& buffer0 = *publisher0_result.value()->buffer();

  auto publisher1_result =
    channel_factory.open_shm_publisher(jewels::Uuid<Tag>::random_uuid().to_string(), "ch1", layout, 2);
  REQUIRE(publisher1_result);
  auto pub_handle1_result = publisher1_result.value()->extract_publisher();
  REQUIRE(pub_handle1_result);
  PublisherHandle pub_handle1 = *std::move(pub_handle1_result);
  Buffer& buffer1 = *publisher1_result.value()->buffer();

  REQUIRE(buffer0.tail() == 0UL);
  REQUIRE(buffer0.head() == 0UL);
  REQUIRE(buffer1.tail() == 0UL);
  REQUIRE(buffer1.head() == 0UL);
  SECTION("Batch process")
  {
    auto reservation0 = pub_handle0.reserve();
    REQUIRE(reservation0);
    auto reservation1 = pub_handle1.reserve();
    REQUIRE(reservation1);

    auto reservations = std::array{*std::move(reservation0), *std::move(reservation1)};
    SECTION("Both commit")
    {
      for (auto& reservation : reservations)
      {
        reservation.slots().begin().mark_for_commit();
      }
      REQUIRE(process_slots(std::span{reservations}, fake_publish_time));
      REQUIRE(buffer0.tail() == 0UL);
      REQUIRE(buffer0.head() == 1UL);
      REQUIRE(buffer0.begin()->header()->publish_timestamp == fake_publish_time.time_since_epoch().count());
      REQUIRE(buffer1.tail() == 0UL);
      REQUIRE(buffer1.head() == 1UL);
      REQUIRE(buffer1.begin()->header()->publish_timestamp == fake_publish_time.time_since_epoch().count());
    }
    SECTION("One commits")
    {
      reservations.front().slots().begin().mark_for_commit();
      REQUIRE(process_slots(std::span{reservations}, fake_publish_time));
      REQUIRE(buffer0.tail() == 0UL);
      REQUIRE(buffer0.head() == 1UL);
      REQUIRE(buffer0.begin()->header()->publish_timestamp == fake_publish_time.time_since_epoch().count());
      REQUIRE(buffer1.tail() == 0UL);
      REQUIRE(buffer1.head() == 0UL);
      REQUIRE(buffer1.begin()->header()->publish_timestamp == PublisherReservation::unset_publish_timestamp);
    }
    SECTION("Fails")
    {
      for (auto index = 0UL; index < reservations.size(); index++)
      {
        DYNAMIC_SECTION("Fail on index: " << index)
        {
          REQUIRE(reservations.at(index).discard());
          REQUIRE_FALSE(process_slots(std::span{reservations}, fake_publish_time));
        }
      }
    }
  }
}

} // namespace clockwork::pinion
