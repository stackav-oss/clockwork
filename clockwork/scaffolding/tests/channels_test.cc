// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/abstract_channel.hh"
#include "clockwork/pinion/buffer_layout.hh"
#include "clockwork/pinion/observer.hh"
#include "clockwork/pinion/shm_channel.hh"
#include "clockwork/pinion/shm_channel_factory.hh"
#include "clockwork/pinion/shm_publisher.hh"
#include "clockwork/pinion/shm_subscriber.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "clockwork/pinion/tests/support/pub_sub.hh"
#include "clockwork/pinion/tests/support/tmp_shm_namespace.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/runners/epoll_manager.hh"
#include "clockwork/scaffolding/abstract_casing.hh"
#include "clockwork/scaffolding/channels.hh"
#include "clockwork/scaffolding/tests/support/mock_casing.hh"
#include "jewels/container/compare.hh"
#include "jewels/memory/default_memory_resource.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/new_delete_memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>
#include <trompeloeil/catch2.hpp> // IWYU pragma: keep (registers catch2 as the error handler)
#include <trompeloeil/mock.hpp>
#include <xxh3.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <memory_resource>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace clockwork::scaffolding
{
namespace
{

#ifdef __has_feature
#if __has_feature(thread_sanitizer)
__attribute__((constructor)) void fix_catch2_cerr_nonthreadsafe_redirect()
{
  // catch2 unilaterally redirects cerr to a plain (non-threadsafe) stringstream if it detects that it's running under
  // bazel with a known output file (which it will then report to).  Under normal circumstances this is acceptable, but
  // it breaks tsan, so we disable it. catch2 doesn't offer a solution, but if the output file envvar is unset then it
  // won't attempt the redirection.
  ::unsetenv("XML_OUTPUT_FILE");
}
#endif
#endif

struct Msg
{
  uint64_t x;
  uint8_t y;
  friend bool operator==(const Msg& lhs, const Msg& rhs)
  {
    return lhs.x == rhs.x && lhs.y == rhs.y;
  }
  [[maybe_unused]] friend std::ostream& operator<<(std::ostream& oss, const Msg& msg)
  {
    return oss << "Msg{x=" << msg.x << ",y=" << static_cast<int>(msg.y) << "}";
  }
};

TEST_CASE("setup_channels")
{
  using testing::dump;
  using testing::publish;
  using testing::TestObserver;

  constexpr uint64_t num_slots_1 = 3;
  constexpr uint64_t num_slots_2 = 5;
  constexpr uint64_t num_subscribers = 10;

  const pinion::support::TmpShmNamespace tmp_namespace;
  auto channel_factory = tmp_namespace.make_factory();

  const auto process_id_1 = jewels::Uuid<common::ProcessInstanceId>::random_uuid();
  const auto process_id_2 = jewels::Uuid<common::ProcessInstanceId>::random_uuid();
  const auto channel_1_id = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  const auto channel_2_id = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  const auto channel_1_sub1_id = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  const auto channel_1_sub2_id = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  const auto channel_2_sub1_id = jewels::Uuid<common::EndpointInstanceId>::random_uuid();

  jewels::memory::NewDeleteMemoryResource memory(0, "channels_test_memres");
  jewels::memory::MemoryResource memres{memory};

  std::vector<Tappy<common::PublishEndpoint<>>> endpoints;
  endpoints.emplace_back();
  endpoints.back().get_mutable_process_id() = process_id_1;
  endpoints.back().get_mutable_publisher_id() = channel_1_id;
  endpoints.back().get_mutable_buffer_layout().get_mutable_num_slots() = num_slots_1;
  endpoints.back().get_mutable_buffer_layout().get_mutable_message_size() = sizeof(Msg);
  endpoints.back().get_mutable_num_subscribers() = num_subscribers;
  endpoints.emplace_back();
  endpoints.back().get_mutable_process_id() = process_id_2;
  endpoints.back().get_mutable_publisher_id() = channel_2_id;
  endpoints.back().get_mutable_buffer_layout().get_mutable_num_slots() = num_slots_2;
  endpoints.back().get_mutable_buffer_layout().get_mutable_message_size() = sizeof(Msg);
  endpoints.back().get_mutable_num_subscribers() = num_subscribers;

  jewels::expected<ChannelMap, jewels::MonoError> channels_1;
  jewels::expected<ChannelMap, jewels::MonoError> channels_2;

  std::atomic<bool> channels_1_ready{false};

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // setup_channels blocks until all subscribed publishers are created so this test needs threading
  std::thread thread_1(
    [&channels_1, &channels_1_ready, &endpoints, &memres, &process_id_1, &channel_factory]()
    {
      channels_1 = setup_channels(endpoints, memres, process_id_1, channel_factory);
      channels_1_ready = true;
    });

  // Without process_2 created, this should be waiting on channel_2 publisher
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  CHECK(channels_1_ready == false);

  // However, process_1's publisher should be created so this should fail immediately.
  CHECK(!setup_channels(endpoints, memres, process_id_1, channel_factory));

  std::thread thread_2([&channels_2, &endpoints, &memres, &process_id_2, &channel_factory]()
                       { channels_2 = setup_channels(endpoints, memres, process_id_2, channel_factory); });

  // process_2 should be able to subscribe to the channel_1 publisher created by process_1 and will unblock process_1 by
  // creating the channel_2 publisher.  Now everything should be able to finish
  thread_1.join();
  thread_2.join();
  REQUIRE(channels_1);
  REQUIRE(channels_2);

  // Fetch the created channels and validate their types
  auto* channel_1_shm_pub = dynamic_cast<pinion::ShmPublisher*>(channels_1->at(channel_1_id).get());
  auto* channel_2_shm_sub = dynamic_cast<pinion::ShmSubscriber*>(channels_1->at(channel_2_id).get());
  auto* channel_1_shm_sub = dynamic_cast<pinion::ShmSubscriber*>(channels_2->at(channel_1_id).get());
  auto* channel_2_shm_pub = dynamic_cast<pinion::ShmPublisher*>(channels_2->at(channel_2_id).get());
  REQUIRE(channel_1_shm_pub);
  REQUIRE(channel_2_shm_sub);
  REQUIRE(channel_1_shm_sub);
  REQUIRE(channel_2_shm_pub);

  for (size_t count = 0; count < num_slots_2; count++)
  {
    publish(channel_1_shm_pub->publisher(), Msg{.x = count, .y = 1});
    publish(channel_2_shm_pub->publisher(), Msg{.x = count, .y = 2});
  }

  const std::vector<Msg> channel_1_expected = {{.x = 2, .y = 1}, {.x = 3, .y = 1}, {.x = 4, .y = 1}};
  const std::vector<Msg> channel_2_expected = {
    {.x = 0, .y = 2}, {.x = 1, .y = 2}, {.x = 2, .y = 2}, {.x = 3, .y = 2}, {.x = 4, .y = 2}};
  CHECK(dump<Msg>(channel_1_shm_sub->make_subscriber()) == channel_1_expected);
  CHECK(dump<Msg>(channel_2_shm_sub->make_subscriber()) == channel_2_expected);

  // Since channel creation is heavyweight, this will just reuse them for testing the connection functions without
  // things like SECTION that would need to recreate the channel map
  using RetT = jewels::expected<void, AbstractCasing::Error>;

  std::shared_ptr<pinion::Observer> fake_observer = std::make_shared<testing::FakeObserver>();
  {
    std::vector<Tappy<common::PubSubConnection>> connections;
    connections.emplace_back();
    connections.back().get_mutable_subscriber_process_id() = process_id_2;
    connections.back().get_mutable_publisher_id() = channel_1_id;
    connections.back().get_mutable_subscriber_id() = channel_1_sub1_id;
    connections.emplace_back();
    connections.back().get_mutable_subscriber_process_id() = process_id_1;
    connections.back().get_mutable_publisher_id() = channel_1_id;
    connections.back().get_mutable_subscriber_id() = channel_1_sub2_id;
    connections.emplace_back();
    connections.back().get_mutable_subscriber_process_id() = process_id_1;
    connections.back().get_mutable_publisher_id() = channel_2_id;
    connections.back().get_mutable_subscriber_id() = channel_2_sub1_id;

    const jewels::expected<std::shared_ptr<pinion::Observer>, AbstractCasing::Error> fake_return = fake_observer;

    {
      MockCasing casing;

      REQUIRE_CALL(casing, try_connect_subscriber(channel_1_sub2_id, ::trompeloeil::_))
        .SIDE_EFFECT(CHECK(dump<Msg>(_2) == channel_1_expected))
        .RETURN(fake_return);
      REQUIRE_CALL(casing, try_connect_subscriber(channel_2_sub1_id, ::trompeloeil::_))
        .SIDE_EFFECT(CHECK(dump<Msg>(_2) == channel_2_expected))
        .RETURN(fake_return);

      auto observers = connect_subscribers(connections, memres, *channels_1, process_id_1, casing);
      REQUIRE(observers);
    }
    {
      MockCasing casing;

      REQUIRE_CALL(casing, try_connect_subscriber(channel_1_sub1_id, ::trompeloeil::_))
        .SIDE_EFFECT(CHECK(dump<Msg>(_2) == channel_1_expected))
        .RETURN(fake_return);

      auto observers = connect_subscribers(connections, memres, *channels_2, process_id_2, casing);
      REQUIRE(observers);
    }
  }

  {
    MockCasing casing;
    REQUIRE_CALL(casing, try_connect_publisher(channel_1_id, ::trompeloeil::_))
      .LR_SIDE_EFFECT(publish(_2, Msg{.x = 999, .y = 11}))
      .RETURN(RetT{});
    CHECK(connect_publishers(endpoints, *channels_1, process_id_1, casing));
    CHECK(dump<Msg>(channel_1_shm_sub->make_subscriber()).back() == Msg{.x = 999, .y = 11});
    CHECK_THROWS(channel_1_shm_pub->publisher());
    CHECK_NOTHROW(channel_2_shm_pub->publisher());

    // Second run should fail due to extracted publisher
    CHECK(!connect_publishers(endpoints, *channels_1, process_id_1, casing));
  }

  {
    MockCasing casing;
    REQUIRE_CALL(casing, try_connect_publisher(channel_2_id, ::trompeloeil::_))
      .LR_SIDE_EFFECT(publish(_2, Msg{.x = 999, .y = 12}))
      .RETURN(RetT{});
    CHECK(connect_publishers(endpoints, *channels_2, process_id_2, casing));
    CHECK(dump<Msg>(channel_2_shm_sub->make_subscriber()).back() == Msg{.x = 999, .y = 12});
    CHECK_THROWS(channel_2_shm_pub->publisher());
  }
}

TEST_CASE("setup deterministic channels")
{
  using testing::dump;
  using testing::publish;
  using testing::TestObserver;

  constexpr uint64_t num_slots_1 = 3;
  constexpr uint64_t num_slots_2 = 5;
  constexpr uint64_t num_subscribers = 10;

  const pinion::support::TmpShmNamespace tmp_namespace;
  auto channel_factory = tmp_namespace.make_factory();

  const auto process_id_1 = jewels::Uuid<common::ProcessInstanceId>::random_uuid();
  const auto channel_1_id = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  const auto channel_2_id = jewels::Uuid<common::EndpointInstanceId>::random_uuid();

  jewels::memory::NewDeleteMemoryResource memory(0, "channels_test_memres");
  const jewels::memory::MemoryResource memres{memory};

  std::vector<Tappy<common::PublishEndpoint<>>> endpoints;
  endpoints.emplace_back();
  endpoints.back().get_mutable_process_id() = process_id_1;
  endpoints.back().get_mutable_publisher_id() = channel_1_id;
  endpoints.back().get_mutable_buffer_layout().get_mutable_num_slots() = num_slots_1;
  endpoints.back().get_mutable_buffer_layout().get_mutable_message_size() = sizeof(Msg);
  endpoints.back().get_mutable_num_subscribers() = num_subscribers;
  endpoints.emplace_back();
  endpoints.back().get_mutable_process_id() = process_id_1;
  endpoints.back().get_mutable_publisher_id() = channel_2_id;
  endpoints.back().get_mutable_buffer_layout().get_mutable_num_slots() = num_slots_2;
  endpoints.back().get_mutable_buffer_layout().get_mutable_message_size() = sizeof(Msg);
  endpoints.back().get_mutable_num_subscribers() = num_subscribers;

  jewels::expected<ChannelMap, jewels::MonoError> channels_1;

  channels_1 = setup_deterministic_channels(endpoints, {}, memres, channel_factory);

  REQUIRE(channels_1);

  // Fetch the created channels and validate their types
  auto* channel_1_shm_pub1 = dynamic_cast<pinion::ShmPublisher*>(channels_1->at(channel_1_id).get());
  auto* channel_2_shm_pub1 = dynamic_cast<pinion::ShmPublisher*>(channels_1->at(channel_2_id).get());
  auto* channel_1_shm_pub2 = dynamic_cast<pinion::ShmPublisher*>(channels_1->at(channel_1_id).get());
  auto* channel_2_shm_pub2 = dynamic_cast<pinion::ShmPublisher*>(channels_1->at(channel_2_id).get());
  REQUIRE(channel_1_shm_pub1);
  REQUIRE(channel_2_shm_pub1);
  REQUIRE(channel_1_shm_pub2);
  REQUIRE(channel_2_shm_pub2);
}
TEST_CASE("bind_channels_to_epoll")
{
  using testing::publish;
  using testing::TestObserver;

  const auto memres{jewels::memory::get_default_memory_resource()};

  const pinion::support::TmpShmNamespace tmp_namespace;
  auto channel_factory = tmp_namespace.make_factory();

  constexpr auto* channel_name = "/channel";
  const auto pub_id = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  const auto sub_id = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  const pinion::BufferLayout layout{.num_slots = 2, .message_size = sizeof(Msg), .is_published_once = false};
  auto pub = channel_factory.open_publisher(pub_id.to_string(), channel_name, layout, 1).value();
  auto sub = channel_factory.open_subscriber(pub_id.to_string(), channel_name, layout, 1).value();

  TestObserver observer;
  REQUIRE(sub->add_observer(jewels::memory::make_non_null_from_ref(observer)));

  EPollManager manager{memres};
  bind_channels_to_epoll({{pub_id, pub}, {sub_id, sub}}, manager);

  // Run epoll loop to pick up the subscriber connect request
  CHECK(pub->num_clients() == 0);
  CHECK(manager.wait(std::chrono::milliseconds(50)));
  CHECK(pub->num_clients() == 1);

  // publish shouldn't trigger an observation on the subscriber...
  publish(pub->publisher(), Msg{.x = 1, .y = 1});
  REQUIRE(!observer.event);

  // ...until the epoll loop is run to dispatch the readable state
  CHECK(manager.wait(std::chrono::milliseconds(50)));
  REQUIRE(observer.event);
}

TEST_CASE("setup_non_connected_channels")
{
  using RetT = jewels::expected<void, AbstractCasing::Error>;

  constexpr uint64_t num_slots = 3;

  const pinion::support::TmpShmNamespace tmp_namespace;
  auto channel_factory = tmp_namespace.make_factory();

  const auto publisher_id = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  const auto subscriber_id = jewels::Uuid<common::EndpointInstanceId>::random_uuid();

  jewels::memory::NewDeleteMemoryResource memory(0, "channels_test_memres");
  const jewels::memory::MemoryResource memres{memory};

  std::vector<Tappy<common::NotConnectedEndpoint>> endpoints;

  // Add a publisher endpoint
  endpoints.emplace_back();
  endpoints.back().get_mutable_endpoint_id() = publisher_id;
  endpoints.back().get_mutable_endpoint_type() = common::NotConnectedEndpointType::publisher;
  endpoints.back().get_mutable_buffer_layout().get_mutable_num_slots() = num_slots;
  endpoints.back().get_mutable_buffer_layout().get_mutable_message_size() = sizeof(Msg);

  // Add a subscriber endpoint
  endpoints.emplace_back();
  endpoints.back().get_mutable_endpoint_id() = subscriber_id;
  endpoints.back().get_mutable_endpoint_type() = common::NotConnectedEndpointType::subscriber;
  endpoints.back().get_mutable_buffer_layout().get_mutable_num_slots() = num_slots;
  endpoints.back().get_mutable_buffer_layout().get_mutable_message_size() = sizeof(Msg);

  MockCasing casing;

  // For the subscriber endpoint, expect set_subscriber to be called
  REQUIRE_CALL(casing, set_subscriber(subscriber_id)).RETURN(RetT{});

  // For the publisher endpoint, expect set_publisher_handle to be called
  REQUIRE_CALL(casing, set_publisher_handle(publisher_id, ::trompeloeil::_)).RETURN(RetT{});

  auto channels = setup_non_connected_channels(endpoints, casing, memres, channel_factory);

  REQUIRE(channels);

  // Only the publisher endpoint should be in the channel map (subscribers are handled via casing)
  CHECK(channels->size() == 1);
  CHECK(channels->count(publisher_id) == 1);
  CHECK(!channels->contains(subscriber_id));

  // Verify that the channel is a publisher
  auto* publisher_ptr = dynamic_cast<pinion::ShmPublisher*>((*channels)[publisher_id].get());
  REQUIRE(publisher_ptr);
}

TEST_CASE("setup_non_connected_channels error cases")
{
  constexpr uint64_t num_slots = 3;

  const pinion::support::TmpShmNamespace tmp_namespace;
  auto channel_factory = tmp_namespace.make_factory();

  jewels::memory::NewDeleteMemoryResource memory(0, "channels_test_memres");
  const jewels::memory::MemoryResource memres{memory};

  SECTION("subscriber set_subscriber fails")
  {
    const auto subscriber_id = jewels::Uuid<common::EndpointInstanceId>::random_uuid();

    std::vector<Tappy<common::NotConnectedEndpoint>> endpoints;
    endpoints.emplace_back();
    endpoints.back().get_mutable_endpoint_id() = subscriber_id;
    endpoints.back().get_mutable_endpoint_type() = common::NotConnectedEndpointType::subscriber;
    endpoints.back().get_mutable_buffer_layout().get_mutable_num_slots() = num_slots;
    endpoints.back().get_mutable_buffer_layout().get_mutable_message_size() = sizeof(Msg);

    MockCasing casing;
    REQUIRE_CALL(casing, set_subscriber(subscriber_id)).RETURN(jewels::unexpected(AbstractCasing::Error{}));

    auto channels = setup_non_connected_channels(endpoints, casing, memres, channel_factory);
    CHECK(!channels);
  }

  SECTION("publisher set_publisher_handle fails")
  {
    const auto publisher_id = jewels::Uuid<common::EndpointInstanceId>::random_uuid();

    std::vector<Tappy<common::NotConnectedEndpoint>> endpoints;
    endpoints.emplace_back();
    endpoints.back().get_mutable_endpoint_id() = publisher_id;
    endpoints.back().get_mutable_endpoint_type() = common::NotConnectedEndpointType::publisher;
    endpoints.back().get_mutable_buffer_layout().get_mutable_num_slots() = num_slots;
    endpoints.back().get_mutable_buffer_layout().get_mutable_message_size() = sizeof(Msg);

    MockCasing casing;
    REQUIRE_CALL(casing, set_publisher_handle(publisher_id, ::trompeloeil::_))
      .RETURN(jewels::unexpected(AbstractCasing::Error{}));

    auto channels = setup_non_connected_channels(endpoints, casing, memres, channel_factory);
    CHECK(!channels);
  }
}

} // namespace
} // namespace clockwork::scaffolding
