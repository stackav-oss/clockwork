// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/observer.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "clockwork/pinion/shm_channel.hh"
#include "clockwork/pinion/shm_channel_factory.hh"
#include "clockwork/pinion/shm_publisher.hh"
#include "clockwork/pinion/shm_subscriber.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "clockwork/pinion/tests/support/epoll_snooper.hh"
#include "clockwork/pinion/tests/support/pub_sub.hh"
#include "clockwork/pinion/tests/support/tmp_shm_namespace.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file.hh"
#include "jewels/filesystem/mmap_region.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <boost/iterator/iterator_facade.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <memory>
#include <memory_resource>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <sys/epoll.h>
#include <sys/mman.h>
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

class ShmChannelOpenHack : public ShmChannel
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
  };

  const std::string channel_uuid_str = jewels::Uuid<Tag>::random_uuid().to_string();
  constexpr auto channel_name = "/test/channel";
  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};

  const support::TmpShmNamespace tmp_namespace;
  const auto& socket_ns = tmp_namespace.get_namespace();
  auto channel_root = Directory::create_open(Directory::at_cwd, tmp_namespace.get_full_path().native());
  REQUIRE(channel_root);

  SECTION("file tests")
  {
    auto open = [&](std::string_view channel_uuid_str, const BufferLayout& layout, ShmChannel::Role role)
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
        };
        auto result = open(channel_uuid_str, bad_layout, role);
        REQUIRE(!result);
        CHECK(result.error() == ShmChannel::Error::dirty);
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

    SECTION("bespoke pubsub")
    {
      auto result = open(channel_uuid_str, buffer_layout, ShmChannel::Role::publisher);
      REQUIRE(result);

      Buffer& buffer = *std::get<1>(result.value());
      PublisherHandle publisher{jewels::memory::make_non_null_from_ref(buffer), max_observer, memres};
      auto subscriber = SubscriberHandle(jewels::memory::make_non_null_from_ref(buffer));

      std::vector<Msg> expected;
      publish<Msg, num_slots>(publisher, expected, 1);
      publish<Msg, num_slots>(publisher, expected, 2);
      CHECK(dump<Msg>(subscriber) == expected);
      CHECK(expected == std::vector{{1U, 2U}});
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
    auto publisher_channel = std::make_shared<ShmPublisher>(*std::move(publisher_result));
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
    CHECK(!publisher_channel->add_observer(jewels::memory::make_non_null_from_ref(subscriber1_events)));
    CHECK(!publisher_channel->extract_publisher());

    // Publish messages, enough to fill and overwrite the ring buffer
    std::vector<Msg> expected;
    for (uint32_t i = 0; i < num_slots + num_overwrite; i++)
    {
      publish<Msg, num_slots>(*publisher, expected, i);
    }

    // Check that the local subscriber reads the expected messages
    CHECK(dump<Msg>(subscriber1) == expected);
    CHECK(subscriber1_events.events.size() == num_slots + num_overwrite);
    CHECK(subscriber1_events.events.front().head == 0);
    CHECK(subscriber1_events.events.front().tail == 0);
    CHECK(subscriber1_events.events.back().head == num_overwrite + num_slots - 1);
    CHECK(subscriber1_events.events.back().tail == num_overwrite);

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
    CHECK(publisher_channel->buffer()->layout().num_slots == buffer_layout.num_slots);
    CHECK(publisher_channel->buffer()->layout().message_size == buffer_layout.message_size);

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
    publisher_channel->close_socket();
    epoll.notify(subscriber_channel->socket(), EPOLLIN);
    CHECK(subscriber_channel->socket() == -1);
  }

  SECTION("factory")
  {
    ShmChannelFactory factory(memres, std::move(*channel_root), tmp_namespace.get_namespace(), resume_behavior);
    auto opener = [&](auto role)
    { return factory.open(role, channel_uuid_str, channel_name, buffer_layout, max_observer); };

    auto shm_publisher = opener(ShmChannel::Role::publisher);
    auto shm_subscriber = opener(ShmChannel::Role::subscriber);
    REQUIRE(shm_publisher);
    REQUIRE(shm_subscriber);

    auto* shm_publisher_ptr = dynamic_cast<ShmPublisher*>(shm_publisher->get());
    REQUIRE(shm_publisher_ptr);

    auto& publisher = shm_publisher_ptr->publisher();
    auto subscriber = shm_subscriber.value()->make_subscriber();

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

    const support::TmpShmNamespace ns1;
    auto ns1_factory = ShmChannelFactory::make(memres, ns1.get_namespace(), ns1.get_full_path().native());
    REQUIRE(ns1_factory);
    auto ns1_publisher =
      ns1_factory->open_publisher(channel_uuid_str, channel_name, buffer_layout, max_observer).value();
    auto ns1_subscriber =
      ns1_factory->open_subscriber(channel_uuid_str, channel_name, buffer_layout, max_observer).value();
    CHECK(ns1_factory->socket_ns() == std::pmr::string("/clockwork/" + ns1.get_namespace() + "/pinion/pub"));
    CHECK(
      std::filesystem::exists(
        ns1.get_full_path() / "clockwork" / ns1.get_namespace() / "pinion/pub" / channel_uuid_str));

    const support::TmpShmNamespace ns2;
    auto ns2_factory = ShmChannelFactory::make(memres, ns2.get_namespace(), ns2.get_full_path().native());
    REQUIRE(ns2_factory);
    auto ns2_publisher =
      ns2_factory->open(Role::publisher, channel_uuid_str, channel_name, buffer_layout, max_observer).value();
    auto ns2_subscriber =
      ns2_factory->open(Role::subscriber, channel_uuid_str, channel_name, buffer_layout, max_observer).value();
    CHECK(ns2_factory->socket_ns() == std::pmr::string("/clockwork/" + ns2.get_namespace() + "/pinion/pub"));
    CHECK(
      std::filesystem::exists(
        ns2.get_full_path() / "clockwork" / ns2.get_namespace() / "pinion/pub" / channel_uuid_str));

    TestObserver ns1_events;
    CHECK(ns1_publisher->add_observer(jewels::memory::make_non_null_from_ref(ns1_events)));
    TestObserver ns2_events;
    CHECK(ns2_publisher->add_observer(jewels::memory::make_non_null_from_ref(ns2_events)));

    std::vector<Msg> ns1_expected;
    publish<Msg, num_slots>(dynamic_cast<ShmPublisher&>(*ns1_publisher).publisher(), ns1_expected, 100);
    std::vector<Msg> ns2_expected;
    publish<Msg, num_slots>(dynamic_cast<ShmPublisher&>(*ns2_publisher).publisher(), ns2_expected, 100);
    publish<Msg, num_slots>(dynamic_cast<ShmPublisher&>(*ns2_publisher).publisher(), ns2_expected, 101);

    CHECK(dump<Msg>(ns1_subscriber->make_subscriber()) == ns1_expected);
    CHECK(dump<Msg>(ns2_subscriber->make_subscriber()) == ns2_expected);
    REQUIRE(ns1_events.events.size() == 1);
    REQUIRE(ns2_events.events.size() == 2);
  }
}

TEST_CASE("Factory registry")
{
  CHECK(pinion::ShmChannelFactoryContext::get() == nullptr);
  {
    const auto tmpns1 = pinion::support::TmpShmNamespace();
    auto factory1 = tmpns1.make_factory();
    const auto context1 = pinion::ShmChannelFactoryContext(factory1);
    CHECK(pinion::ShmChannelFactoryContext::get() == &factory1);
    {
      const auto tmpns2 = pinion::support::TmpShmNamespace();
      auto factory2 = tmpns2.make_factory();
      const auto context2 = pinion::ShmChannelFactoryContext(factory2);
      CHECK(pinion::ShmChannelFactoryContext::get() == &factory2);
    }
    CHECK(pinion::ShmChannelFactoryContext::get() == &factory1);
  }
  CHECK(pinion::ShmChannelFactoryContext::get() == nullptr);
}

TEST_CASE("Reconnect when resume is allowed")
{
  constexpr BufferLayout layout{
    .num_slots = 2UL,
    .message_size = sizeof(uint64_t),
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

  auto publisher_result = factory_result->open_publisher(channel_uuid_str, channel_name, layout, 1U);
  REQUIRE(publisher_result);
  auto publisher = *std::move(publisher_result);
  REQUIRE(epoll.add(publisher->socket(), EPOLLIN, publisher));

  auto subscriber_result = factory_result->open_subscriber(channel_uuid_str, channel_name, layout, 1U);
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
    auto slot = publisher->publisher().reserve();
    REQUIRE(slot);
    std::memcpy(slot->slot().message().data(), &message1, sizeof(uint64_t));
    REQUIRE(slot->commit(fake_send_timestamp));
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

  publisher_result = factory_result->open_publisher(channel_uuid_str, channel_name, layout, 1U);
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
    auto slot = publisher->publisher().reserve();
    REQUIRE(slot);
    std::memcpy(slot->slot().message().data(), &message2, sizeof(uint64_t));
    REQUIRE(slot->commit(fake_send_timestamp));
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
  };
  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  const jewels::testing::TmpDirectoryGuard shm_dir;
  const auto socket_ns = jewels::Uuid<void>::random_uuid().to_string();
  const auto channel_uuid_str = jewels::Uuid<void>::random_uuid().to_string();
  constexpr auto channel_name = "/test/channel";

  auto factory_result =
    ShmChannelFactory::make(memres, socket_ns, shm_dir.get_path().string(), ShmChannel::ResumeBehavior::no_resume);
  auto publisher_result = factory_result->open_publisher(channel_uuid_str, channel_name, layout, 1U);
  REQUIRE(publisher_result);
  auto publisher = *std::move(publisher_result);
  publisher.reset();
  publisher_result = factory_result->open_publisher(channel_uuid_str, channel_name, layout, 1U);
  REQUIRE(publisher_result.error() == ShmChannel::Error::dirty);
}
} // namespace clockwork::pinion
