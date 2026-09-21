// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/logging/schema_encoding_clk_cc.hh"
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/abstract_channel.hh"
#include "clockwork/pinion/buffer_layout.hh"
#include "clockwork/pinion/detail/socket_common.hh"
#include "clockwork/pinion/detail/tcp_socket.hh"
#include "clockwork/pinion/detail/unix_socket.hh"
#include "clockwork/pinion/shm_channel.hh"
#include "clockwork/pinion/shm_channel_factory.hh"
#include "clockwork/pinion/shm_publisher.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/tcp_bridge_common.hh"
#include "clockwork/pinion/tcp_bridge_config_clk_cc.hh"
#include "clockwork/pinion/tcp_bridge_server.hh"
#include "clockwork/pinion/tests/support/bridge_test_message_clk_cc.hh"
#include "clockwork/pinion/tests/support/bridge_test_support.hh"
#include "clockwork/pinion/tests/support/epoll_snooper.hh"
#include "clockwork/pinion/tests/support/pub_sub.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/container/compare.hh"
#include "jewels/container/tap/var_array.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/filesystem/filesystem.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/math/constants.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/networking/socket_address.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/fix_catch2_cerr_nonthreadsafe_redirect.hh" // IWYU pragma: keep
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <compare>
#include <cstddef>
#include <memory>
#include <memory_resource>
#include <ratio>
#include <span>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <thread>
#include <unordered_set>
#include <utility>

namespace clockwork::pinion
{

TEST_CASE("TcpBridgeServer | Simple Send/Recv")
{
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto test_dir_path = test_dir.get_path().string();
  const auto socket_ns = jewels::Uuid<void>::random_uuid().to_string();

  const auto is_bulk_data = GENERATE(false, true);
  CAPTURE(is_bulk_data);
  constexpr auto message_size = jewels::math::constants::bytes_per_kb<size_t> + 1U;
  using Msg = std::array<std::byte, message_size>;
  constexpr size_t num_slots = 3;
  constexpr size_t max_observers = 3;

  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  auto channel_factory_result = ShmChannelFactory::make(memres, socket_ns, test_dir_path);
  REQUIRE(channel_factory_result);
  const auto channel_factory = std::make_shared<ShmChannelFactory>(*std::move(channel_factory_result));

  constexpr auto channel_name = "test_channel";
  const auto channel_uuid = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  BufferLayout layout{
    .num_slots = num_slots,
    .message_size = sizeof(Msg),
    .is_published_once = false,
  };

  const auto publisher_result =
    channel_factory->open_publisher(channel_uuid.to_string(), channel_name, layout, max_observers);
  REQUIRE(publisher_result);
  auto& publisher = dynamic_cast<ShmPublisher&>(*publisher_result.value());

  EPollSnooper epoll{};

  // Create the bridge.
  constexpr size_t max_clients = 3;
  auto config = std::make_unique<Tappy<TcpBridgeServerConfig>>();
  config->set_publisher_id(channel_uuid);
  config->get_mutable_buffer_layout().set_num_slots(num_slots);
  config->get_mutable_buffer_layout().set_message_size(sizeof(Msg));
  config->get_mutable_buffer_layout().set_is_published_once(false);
  config->get_underlying_listen_address().set_truncate(support::local_socket_host);
  config->set_listen_port(0U);
  config->set_num_clients(max_clients);
  config->get_underlying_channel_name().set_truncate(channel_name);
  config->set_is_bulk_data(is_bulk_data);
  const auto schema_encoding = LoggingTraits<Tappy<support::BridgeTestMessage<message_size>>>::schema_encoding;
  const auto& schema_definition = LoggingTraits<Tappy<support::BridgeTestMessage<message_size>>>::schema_definition;
  config->set_schema_encoding(static_cast<clockwork_logging::SchemaEncoding>(schema_encoding));
  config->get_underlying_schema_definition().resize(schema_definition.size());
  std::ranges::copy(std::as_bytes(std::span{schema_definition}), config->get_mutable_schema_definition().begin());

  const auto diagnostics_state = std::make_shared<TcpBridgeDiagnosticsState>();

  auto bridge_server = TcpBridgeServer::make(
    memres,
    *config,
    channel_factory,
    jewels::memory::make_non_null_from_ref(epoll),
    diagnostics_state,
    TcpBridgeServer::Mode::unit_test);
  REQUIRE(bridge_server);
  config.reset();
  const auto listen_addr =
    jewels::networking::SocketAddress::create(std::string{support::local_socket_host}, bridge_server->listen_port());
  const auto ephemeral_addr = jewels::networking::SocketAddress::create(std::string{support::local_socket_host}, 0);

  SECTION("Connect and Send")
  {
    auto tcp_client = TcpSocket::create_connect(*listen_addr, *ephemeral_addr);
    REQUIRE(tcp_client);
    // Set non-blocking so we can fail fast if the server is busted.
    REQUIRE(set_nonblocking(tcp_client->descriptor(), true));

    // Tell the server that a a client is waiting to be accepted.
    bridge_server->notify(epoll, bridge_server->listen_fd(), EPOLLIN);
    CHECK(support::recv_null_header(tcp_client->descriptor(), 0U));

    // Tell the publisher to accept the connection from the bridge
    publisher.notify(epoll, -1, EPOLLIN);

    const auto start_time = jewels::time::SteadyClock::now();

    // Send some messages and check that they arrive in order
    const auto msg2 = support::make_random_message<message_size>();
    testing::publish(
      publisher.publisher(),
      *msg2,
      jewels::time::SyncTime{std::chrono::nanoseconds{1234L}},
      2,
      jewels::time::SyncTime{std::chrono::nanoseconds{2345L}});
    CHECK(support::check_next_payload(tcp_client->descriptor(), 2, *msg2, 1234L, 2345L));
    const auto msg3 = support::make_random_message<message_size>();
    testing::publish(
      publisher.publisher(),
      *msg3,
      jewels::time::SyncTime{std::chrono::nanoseconds{5677L}},
      3,
      jewels::time::SyncTime{std::chrono::nanoseconds{6788L}});
    const auto msg4 = support::make_random_message<message_size>();
    testing::publish(
      publisher.publisher(),
      *msg4,
      jewels::time::SyncTime{std::chrono::nanoseconds{5678L}},
      4,
      jewels::time::SyncTime{std::chrono::nanoseconds{6789L}});
    CHECK(support::check_next_payload(tcp_client->descriptor(), 3, *msg3, 5677L, 6788L));
    CHECK(support::check_next_payload(tcp_client->descriptor(), 4, *msg4, 5678L, 6789L));

    if (is_bulk_data)
    {
      const auto end_time = jewels::time::SteadyClock::now();
      REQUIRE(end_time - start_time > 3 * max_bridge_bulk_data_transmit_delay);
    }

    const auto diagnostics_counters = diagnostics_state->get_and_reset_counters();
    CHECK(support::compare_diagnostics_counters(diagnostics_counters, TcpBridgeDiagnosticsCounters{}));
  }

  SECTION("Server only sends newest message after connect")
  {
    auto tcp_client = TcpSocket::create_connect(*listen_addr, *ephemeral_addr);
    REQUIRE(tcp_client);
    // Set non-blocking so we can fail fast if the server is busted.
    REQUIRE(set_nonblocking(tcp_client->descriptor(), true));

    // Send some messages before the client connects, the client should only receive the last one
    const auto msg0 = support::make_random_message<message_size>();
    testing::publish(
      publisher.publisher(),
      *msg0,
      jewels::time::SyncTime{std::chrono::nanoseconds{1232L}},
      0,
      jewels::time::SyncTime{std::chrono::nanoseconds{2343L}});
    const auto msg1 = support::make_random_message<message_size>();
    testing::publish(
      publisher.publisher(),
      *msg1,
      jewels::time::SyncTime{std::chrono::nanoseconds{1233L}},
      1,
      jewels::time::SyncTime{std::chrono::nanoseconds{2344L}});
    const auto msg2 = support::make_random_message<message_size>();
    testing::publish(
      publisher.publisher(),
      *msg2,
      jewels::time::SyncTime{std::chrono::nanoseconds{1234L}},
      2,
      jewels::time::SyncTime{std::chrono::nanoseconds{2345L}});

    // Tell the server that a a client is waiting to be accepted.
    bridge_server->notify(epoll, bridge_server->listen_fd(), EPOLLIN);

    // Tell the publisher to accept the connection from the bridge
    publisher.notify(epoll, -1, EPOLLIN);

    // Server will send the first message immediately after accepting the connection
    CHECK(support::check_next_payload(tcp_client->descriptor(), 2, *msg2, 1234L, 2345L));

    const auto msg3 = support::make_random_message<message_size>();
    testing::publish(
      publisher.publisher(),
      *msg3,
      jewels::time::SyncTime{std::chrono::nanoseconds{5677L}},
      3,
      jewels::time::SyncTime{std::chrono::nanoseconds{6788L}});
    const auto msg4 = support::make_random_message<message_size>();
    testing::publish(
      publisher.publisher(),
      *msg4,
      jewels::time::SyncTime{std::chrono::nanoseconds{5678L}},
      4,
      jewels::time::SyncTime{std::chrono::nanoseconds{6789L}});
    CHECK(support::check_next_payload(tcp_client->descriptor(), 3, *msg3, 5677L, 6788L));
    CHECK(support::check_next_payload(tcp_client->descriptor(), 4, *msg4, 5678L, 6789L));

    const auto diagnostics_counters = diagnostics_state->get_and_reset_counters();
    CHECK(support::compare_diagnostics_counters(diagnostics_counters, TcpBridgeDiagnosticsCounters{}));
  }

  SECTION("Fanout")
  {
    CHECK(support::check_num_clients(*bridge_server, 0U));

    auto client1 = TcpSocket::create_connect(*listen_addr, *ephemeral_addr);
    REQUIRE(client1);
    bridge_server->notify(epoll, bridge_server->listen_fd(), EPOLLIN);
    publisher.notify(epoll, -1, EPOLLIN);
    CHECK(support::check_num_clients(*bridge_server, 1U));
    CHECK(support::recv_null_header(client1->descriptor(), 0U));

    const auto msg0 = support::make_random_message<message_size>();
    testing::publish(publisher.publisher(), *msg0);
    CHECK(support::check_next_payload(client1->descriptor(), 0, *msg0));

    const auto msg1 = support::make_random_message<message_size>();
    testing::publish(publisher.publisher(), *msg1);

    auto client2 = TcpSocket::create_connect(*listen_addr, *ephemeral_addr);
    REQUIRE(client2);
    bridge_server->notify(epoll, bridge_server->listen_fd(), EPOLLIN);
    publisher.notify(epoll, -1, EPOLLIN);
    CHECK(support::check_num_clients(*bridge_server, 2U));

    CHECK(support::check_next_payload(client1->descriptor(), 1, *msg1));
    CHECK(support::check_next_payload(client2->descriptor(), 1, *msg1));

    const auto msg2 = support::make_random_message<message_size>();
    testing::publish(publisher.publisher(), *msg2);

    auto client3 = TcpSocket::create_connect(*listen_addr, *ephemeral_addr);
    REQUIRE(client3);
    bridge_server->notify(epoll, bridge_server->listen_fd(), EPOLLIN);
    publisher.notify(epoll, -1, EPOLLIN);
    CHECK(support::check_num_clients(*bridge_server, 3U));

    // This one should get discarded by the bridge.
    auto client4 = TcpSocket::create_connect(*listen_addr, *ephemeral_addr);
    REQUIRE(client4);
    bridge_server->notify(epoll, bridge_server->listen_fd(), EPOLLIN);
    publisher.notify(epoll, -1, EPOLLIN);
    CHECK(support::check_num_clients(*bridge_server, 3U));

    CHECK(support::check_next_payload(client1->descriptor(), 2, *msg2));
    CHECK(support::check_next_payload(client2->descriptor(), 2, *msg2));
    CHECK(support::check_next_payload(client3->descriptor(), 2, *msg2));

    CHECK(client3->release_descriptor().close());
    CHECK(support::check_num_clients(*bridge_server, 2U));

    const auto msg3 = support::make_random_message<message_size>();
    testing::publish(publisher.publisher(), *msg3);

    CHECK(support::check_next_payload(client1->descriptor(), 3, *msg3));
    CHECK(support::check_next_payload(client2->descriptor(), 3, *msg3));

    // This one should work now that client3 has closed.
    auto client5 = TcpSocket::create_connect(*listen_addr, *ephemeral_addr);
    REQUIRE(client5);
    bridge_server->notify(epoll, bridge_server->listen_fd(), EPOLLIN);
    publisher.notify(epoll, -1, EPOLLIN);
    CHECK(bridge_server->get_num_clients() == 3U);

    CHECK(support::check_next_payload(client5->descriptor(), 3, *msg3));
    const auto diagnostics_counters = diagnostics_state->get_and_reset_counters();
    CHECK(
      support::compare_diagnostics_counters(
        diagnostics_counters, TcpBridgeDiagnosticsCounters{.closed_socket_count = 1U}));
  }
}

TEST_CASE("TcpBridgeServer | Overrun")
{
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto test_dir_path = test_dir.get_path().string();
  const auto socket_ns = jewels::Uuid<void>::random_uuid().to_string();

  constexpr auto message_size = jewels::math::constants::bytes_per_kb<size_t>;
  using Msg = std::array<std::byte, message_size>;
  constexpr size_t num_slots = 3;
  constexpr size_t max_observers = 1;

  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  jewels::filesystem::Filesystem filesys{memres};
  auto channel_factory_result = ShmChannelFactory::make(memres, socket_ns, test_dir_path);
  REQUIRE(channel_factory_result);
  const auto channel_factory = std::make_shared<ShmChannelFactory>(*std::move(channel_factory_result));

  constexpr auto channel_name = "test_channel";
  const auto channel_uuid = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  const auto linked_channel_uuid = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  BufferLayout layout{
    .num_slots = num_slots,
    .message_size = sizeof(Msg),
    .is_published_once = false,
  };

  const auto publisher_result =
    channel_factory->open_publisher(channel_uuid.to_string(), channel_name, layout, max_observers);
  REQUIRE(publisher_result);
  auto& publisher = dynamic_cast<ShmPublisher&>(*publisher_result.value());

  // Create a hard link from the linked channel buffer to the actual channel buffer
  const auto channel_path = test_dir.get_path() / "clockwork" / socket_ns / "pinion/pub" / channel_uuid.to_string();
  const auto linked_channel_path =
    test_dir.get_path() / "clockwork" / socket_ns / "pinion/pub" / linked_channel_uuid.to_string();
  REQUIRE(filesys.create_hardlink(channel_path, linked_channel_path));

  // Create a unix socket to send notifications to the bridge
  const auto notify_socket_path =
    fmt::format("/clockwork/{}/pinion/pub/{}", socket_ns, linked_channel_uuid.to_string());
  auto listen_socket_result = UnixSocket::create_bind(notify_socket_path);
  REQUIRE(listen_socket_result);
  auto& listen_socket = listen_socket_result.value();
  REQUIRE(set_nonblocking(listen_socket.descriptor(), true));
  REQUIRE(listen_socket.listen(1));

  EPollSnooper epoll{};

  // Create the bridge.
  constexpr size_t max_clients = 3;
  auto config = std::make_unique<Tappy<TcpBridgeServerConfig>>();
  config->set_publisher_id(linked_channel_uuid);
  config->get_mutable_buffer_layout().set_num_slots(num_slots);
  config->get_mutable_buffer_layout().set_message_size(sizeof(Msg));
  config->get_underlying_listen_address().set_truncate(support::local_socket_host);
  config->set_listen_port(0U);
  config->set_num_clients(max_clients);
  config->get_underlying_channel_name().set_truncate(channel_name);
  const auto schema_encoding = LoggingTraits<Tappy<support::BridgeTestMessage<message_size>>>::schema_encoding;
  const auto& schema_definition = LoggingTraits<Tappy<support::BridgeTestMessage<message_size>>>::schema_definition;
  config->set_schema_encoding(static_cast<clockwork_logging::SchemaEncoding>(schema_encoding));
  config->get_underlying_schema_definition().resize(schema_definition.size());
  std::ranges::copy(std::as_bytes(std::span{schema_definition}), config->get_mutable_schema_definition().begin());

  const auto diagnostics_state = std::make_shared<TcpBridgeDiagnosticsState>();

  auto bridge_server = TcpBridgeServer::make(
    memres,
    *config,
    channel_factory,
    jewels::memory::make_non_null_from_ref(epoll),
    diagnostics_state,
    TcpBridgeServer::Mode::unit_test);
  REQUIRE(bridge_server);
  config.reset();
  const auto listen_addr =
    jewels::networking::SocketAddress::create(std::string{support::local_socket_host}, bridge_server->listen_port());
  const auto ephemeral_addr = jewels::networking::SocketAddress::create(std::string{support::local_socket_host}, 0);

  auto tcp_client = TcpSocket::create_connect(*listen_addr, *ephemeral_addr);
  REQUIRE(tcp_client);
  // Set non-blocking so we can fail fast if the server is busted.
  REQUIRE(set_nonblocking(tcp_client->descriptor(), true));

  // Tell the server that a a client is waiting to be accepted.
  bridge_server->notify(epoll, bridge_server->listen_fd(), EPOLLIN);

  // Accept the connection for the notify socket
  jewels::filesystem::FileDescriptor notify_socket{::accept(listen_socket.descriptor(), nullptr, nullptr)};
  REQUIRE(notify_socket);
  REQUIRE(set_nonblocking(*notify_socket, true));
  ShmChannel::NotifyMsg msg{};
  CHECK(support::recv_null_header(tcp_client->descriptor(), 0U));

  // Send some messages and check that they arrive in order
  const auto msg1 = support::make_random_message<message_size>();
  testing::publish(
    publisher.publisher(),
    *msg1,
    jewels::time::SyncTime{std::chrono::nanoseconds{1234L}},
    1,
    jewels::time::SyncTime{std::chrono::nanoseconds{2345L}});
  const auto msg2 = support::make_random_message<message_size>();
  testing::publish(
    publisher.publisher(),
    *msg2,
    jewels::time::SyncTime{std::chrono::nanoseconds{5678L}},
    2,
    jewels::time::SyncTime{std::chrono::nanoseconds{6789L}});
  REQUIRE(::send(*notify_socket, &msg, sizeof(msg), 0) == sizeof(msg));
  CHECK(support::check_next_payload(tcp_client->descriptor(), 1, *msg1, 1234L, 2345L));
  CHECK(support::check_next_payload(tcp_client->descriptor(), 2, *msg2, 5678L, 6789L));

  // Overrun the buffer and check that the server jumps to the end of the buffer
  const auto msg3 = support::make_random_message<message_size>();
  testing::publish(
    publisher.publisher(),
    *msg3,
    jewels::time::SyncTime{std::chrono::nanoseconds{5680L}},
    3,
    jewels::time::SyncTime{std::chrono::nanoseconds{6790L}});
  std::this_thread::sleep_for(std::chrono::milliseconds(250));
  const auto msg4 = support::make_random_message<message_size>();
  testing::publish(
    publisher.publisher(),
    *msg4,
    jewels::time::SyncTime{std::chrono::nanoseconds{5681L}},
    4,
    jewels::time::SyncTime{std::chrono::nanoseconds{6791L}});
  const auto msg5 = support::make_random_message<message_size>();
  testing::publish(
    publisher.publisher(),
    *msg5,
    jewels::time::SyncTime{std::chrono::nanoseconds{5682L}},
    5,
    jewels::time::SyncTime{std::chrono::nanoseconds{6792L}});
  const auto msg6 = support::make_random_message<message_size>();
  testing::publish(
    publisher.publisher(),
    *msg6,
    jewels::time::SyncTime{std::chrono::nanoseconds{5683L}},
    6,
    jewels::time::SyncTime{std::chrono::nanoseconds{6793L}});

  REQUIRE(::send(*notify_socket, &msg, sizeof(msg), 0) == sizeof(msg));
  CHECK(support::check_next_payload(tcp_client->descriptor(), 6, *msg6, 5683L, 6793L));

  const auto diagnostics_counters = diagnostics_state->get_and_reset_counters();
  CHECK(support::compare_diagnostics_counters(diagnostics_counters, TcpBridgeDiagnosticsCounters{.drop_count = 3U}));
}

TEST_CASE("TcpBridgeServer | Send null header until acked and send keep-alives periodically")
{
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto test_dir_path = test_dir.get_path().string();
  const auto socket_ns = jewels::Uuid<void>::random_uuid().to_string();

  constexpr auto message_size = jewels::math::constants::bytes_per_kb<size_t>;
  using Msg = std::array<std::byte, message_size>;
  constexpr size_t num_slots = 3;
  constexpr size_t max_observers = 1;

  constexpr auto channel_name = "test_channel";
  const auto channel_uuid = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  BufferLayout layout{
    .num_slots = num_slots,
    .message_size = sizeof(Msg),
    .is_published_once = false,
  };

  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  auto channel_factory_result = ShmChannelFactory::make(memres, socket_ns, test_dir_path);
  REQUIRE(channel_factory_result);
  const auto channel_factory = std::make_shared<ShmChannelFactory>(*std::move(channel_factory_result));

  const auto publisher_result =
    channel_factory->open_publisher(channel_uuid.to_string(), channel_name, layout, max_observers);
  REQUIRE(publisher_result);
  auto& publisher = dynamic_cast<ShmPublisher&>(*publisher_result.value());

  EPollSnooper epoll{};

  // Create the bridge.
  constexpr size_t max_clients = 3;
  auto config = std::make_unique<Tappy<TcpBridgeServerConfig>>();
  config->set_publisher_id(channel_uuid);
  config->get_mutable_buffer_layout().set_num_slots(num_slots);
  config->get_mutable_buffer_layout().set_message_size(sizeof(Msg));
  config->get_underlying_listen_address().set_truncate(support::local_socket_host);
  config->set_listen_port(0U);
  config->set_num_clients(max_clients);
  config->get_underlying_channel_name().set_truncate(channel_name);
  const auto schema_encoding = LoggingTraits<Tappy<support::BridgeTestMessage<message_size>>>::schema_encoding;
  const auto& schema_definition = LoggingTraits<Tappy<support::BridgeTestMessage<message_size>>>::schema_definition;
  config->set_schema_encoding(static_cast<clockwork_logging::SchemaEncoding>(schema_encoding));
  config->get_underlying_schema_definition().resize(schema_definition.size());
  std::ranges::copy(std::as_bytes(std::span{schema_definition}), config->get_mutable_schema_definition().begin());

  const auto diagnostics_state = std::make_shared<TcpBridgeDiagnosticsState>();

  auto bridge_server = TcpBridgeServer::make(
    memres,
    *config,
    channel_factory,
    jewels::memory::make_non_null_from_ref(epoll),
    diagnostics_state,
    TcpBridgeServer::Mode::unit_test);
  REQUIRE(bridge_server);
  config.reset();
  const auto listen_addr =
    jewels::networking::SocketAddress::create(std::string{support::local_socket_host}, bridge_server->listen_port());
  const auto ephemeral_addr = jewels::networking::SocketAddress::create(std::string{support::local_socket_host}, 0);

  auto tcp_client = TcpSocket::create_connect(*listen_addr, *ephemeral_addr);
  REQUIRE(tcp_client);
  // Set non-blocking so we can fail fast if the server is busted.
  REQUIRE(set_nonblocking(tcp_client->descriptor(), true));

  // Tell the server that a a client is waiting to be accepted.
  bridge_server->notify(epoll, bridge_server->listen_fd(), EPOLLIN);
  CHECK(support::recv_null_header(tcp_client->descriptor(), 0U));

  // Tell the publisher to accept the connection from the bridge
  publisher.notify(epoll, -1, EPOLLIN);

  // Send some messages and check that they arrive in order.
  const auto msg2 = support::make_random_message<message_size>();
  testing::publish(
    publisher.publisher(),
    *msg2,
    jewels::time::SyncTime{std::chrono::nanoseconds{1234L}},
    2,
    jewels::time::SyncTime{std::chrono::nanoseconds{2345L}});
  CHECK(
    support::check_next_payload(tcp_client->descriptor(), 2, *msg2, 1234L, 2345L, support::AckOption::dont_send_ack));
  const auto msg4 = support::make_random_message<message_size>();
  testing::publish(
    publisher.publisher(),
    *msg4,
    jewels::time::SyncTime{std::chrono::nanoseconds{5678L}},
    4,
    jewels::time::SyncTime{std::chrono::nanoseconds{6789L}});
  CHECK(
    support::check_next_payload(tcp_client->descriptor(), 4, *msg4, 5678L, 6789L, support::AckOption::dont_send_ack));

  // Server should send null headers until ack is received
  CHECK_FALSE(
    support::recv_null_header(tcp_client->descriptor(), 4U, {PayloadType::keep_alive}, support::short_recv_timeout));
  CHECK(support::recv_null_header(tcp_client->descriptor(), 4U, {PayloadType::null_header}));

  // Send an ack for the first message, server should still keep sending null headers
  CHECK(support::send_acknowledgement(tcp_client->descriptor(), 2));
  CHECK_FALSE(
    support::recv_null_header(tcp_client->descriptor(), 4U, {PayloadType::keep_alive}, support::short_recv_timeout));
  CHECK(support::recv_null_header(tcp_client->descriptor(), 4U, {PayloadType::null_header}));

  // Send an ack for the last message, server should stop sending null headers and start sending keepalives
  CHECK(support::send_acknowledgement(tcp_client->descriptor(), 4));
  CHECK(
    support::recv_null_header(
      tcp_client->descriptor(),
      4U,
      {PayloadType::keep_alive},
      tcp_bridge_keep_alive_interval + std::chrono::seconds(1)));

  const auto diagnostics_counters = diagnostics_state->get_and_reset_counters();
  CHECK(support::compare_diagnostics_counters(diagnostics_counters, TcpBridgeDiagnosticsCounters{}));
}

TEST_CASE("TcpBridgeServer | Disconnect if client is not sending acks")
{
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto test_dir_path = test_dir.get_path().string();
  const auto socket_ns = jewels::Uuid<void>::random_uuid().to_string();

  constexpr auto message_size = jewels::math::constants::bytes_per_kb<size_t>;
  using Msg = std::array<std::byte, message_size>;
  constexpr size_t num_slots = 3;
  constexpr size_t max_observers = 1;

  constexpr auto channel_name = "test_channel";
  const auto channel_uuid = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  BufferLayout layout{
    .num_slots = num_slots,
    .message_size = sizeof(Msg),
    .is_published_once = false,
  };

  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  auto channel_factory_result = ShmChannelFactory::make(memres, socket_ns, test_dir_path);
  REQUIRE(channel_factory_result);
  const auto channel_factory = std::make_shared<ShmChannelFactory>(*std::move(channel_factory_result));

  const auto publisher_result =
    channel_factory->open_publisher(channel_uuid.to_string(), channel_name, layout, max_observers);
  REQUIRE(publisher_result);
  auto& publisher = dynamic_cast<ShmPublisher&>(*publisher_result.value());

  EPollSnooper epoll{};

  // Create the bridge.
  constexpr size_t max_clients = 3;
  auto config = std::make_unique<Tappy<TcpBridgeServerConfig>>();
  config->set_publisher_id(channel_uuid);
  config->get_mutable_buffer_layout().set_num_slots(num_slots);
  config->get_mutable_buffer_layout().set_message_size(sizeof(Msg));
  config->get_underlying_listen_address().set_truncate(support::local_socket_host);
  config->set_listen_port(0U);
  config->set_num_clients(max_clients);
  config->get_underlying_channel_name().set_truncate(channel_name);
  const auto schema_encoding = LoggingTraits<Tappy<support::BridgeTestMessage<message_size>>>::schema_encoding;
  const auto& schema_definition = LoggingTraits<Tappy<support::BridgeTestMessage<message_size>>>::schema_definition;
  config->set_schema_encoding(static_cast<clockwork_logging::SchemaEncoding>(schema_encoding));
  config->get_underlying_schema_definition().resize(schema_definition.size());
  std::ranges::copy(std::as_bytes(std::span{schema_definition}), config->get_mutable_schema_definition().begin());

  const auto diagnostics_state = std::make_shared<TcpBridgeDiagnosticsState>();

  auto bridge_server = TcpBridgeServer::make(
    memres, *config, channel_factory, jewels::memory::make_non_null_from_ref(epoll), diagnostics_state);
  REQUIRE(bridge_server);
  config.reset();
  const auto listen_addr =
    jewels::networking::SocketAddress::create(std::string{support::local_socket_host}, bridge_server->listen_port());
  const auto ephemeral_addr = jewels::networking::SocketAddress::create(std::string{support::local_socket_host}, 0);

  auto tcp_client = TcpSocket::create_connect(*listen_addr, *ephemeral_addr);
  REQUIRE(tcp_client);
  // Set non-blocking so we can fail fast if the server is busted.
  REQUIRE(set_nonblocking(tcp_client->descriptor(), true));

  // Tell the server that a a client is waiting to be accepted.
  bridge_server->notify(epoll, bridge_server->listen_fd(), EPOLLIN);
  CHECK(support::recv_null_header(tcp_client->descriptor(), 0U));

  // Tell the publisher to accept the connection from the bridge
  publisher.notify(epoll, -1, EPOLLIN);

  // Send a message and check that it arrives
  const auto msg2 = support::make_random_message<message_size>();
  testing::publish(
    publisher.publisher(),
    *msg2,
    jewels::time::SyncTime{std::chrono::nanoseconds{1234L}},
    2,
    jewels::time::SyncTime{std::chrono::nanoseconds{2345L}});
  CHECK(support::check_next_payload(tcp_client->descriptor(), 2, *msg2, 1234L, 2345L));

  // Send a message and check that it arrives but do not send an acknowledgement
  const auto msg4 = support::make_random_message<message_size>();
  testing::publish(
    publisher.publisher(),
    *msg4,
    jewels::time::SyncTime{std::chrono::nanoseconds{5678L}},
    4,
    jewels::time::SyncTime{std::chrono::nanoseconds{6789L}});
  CHECK(
    support::check_next_payload(tcp_client->descriptor(), 4, *msg4, 5678L, 6789L, support::AckOption::dont_send_ack));

  // Server should disconnect after the timeout
  CHECK(
    support::check_num_clients(
      *bridge_server, 0U, TcpBridgeServer::bridge_server_recv_ack_timeout + std::chrono::seconds(1)));

  // Diagnostics should show the connection was closed
  const auto diagnostics_counters = diagnostics_state->get_and_reset_counters();
  CHECK(
    support::compare_diagnostics_counters(
      diagnostics_counters, TcpBridgeDiagnosticsCounters{.closed_socket_count = 1U}));
}

TEST_CASE("TcpBridgeServer | Large messages")
{
  const jewels::testing::TmpDirectoryGuard test_dir;
  const auto test_dir_path = test_dir.get_path().string();
  const auto socket_ns = jewels::Uuid<void>::random_uuid().to_string();

  // The server has a send timeout used to detect that the client has stopped receiving.
  // When test servers are loaded it sometimes takes more than the timeout for the client
  // to process a message and start receiving the next one causing the send for the next
  // message to time out. 'max_valid_recv_time` is used to detect when the client is not
  // keeping up so we don't hit any timeouts when the test servers are loaded.
  static constexpr auto max_valid_recv_time =
    std::chrono::duration_cast<std::chrono::nanoseconds>(TcpBridgeServer::bridge_server_send_timeout) * 3 / 2;

  constexpr auto message_size = 32U * jewels::math::constants::bytes_per_mb<size_t>;
  using Msg = std::array<std::byte, message_size>;
  constexpr size_t num_slots = 2;
  constexpr size_t max_observers = 1;

  constexpr auto channel_name = "test_channel";
  const auto channel_uuid = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  BufferLayout layout{
    .num_slots = num_slots,
    .message_size = sizeof(Msg),
    .is_published_once = false,
  };

  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  auto channel_factory_result = ShmChannelFactory::make(memres, socket_ns, test_dir_path);
  REQUIRE(channel_factory_result);
  const auto channel_factory = std::make_shared<ShmChannelFactory>(*std::move(channel_factory_result));

  const auto publisher_result =
    channel_factory->open_publisher(channel_uuid.to_string(), channel_name, layout, max_observers);
  REQUIRE(publisher_result);
  auto& publisher = dynamic_cast<ShmPublisher&>(*publisher_result.value());

  EPollSnooper epoll{};

  // Create the bridge.
  constexpr auto max_clients = 1;
  auto config = std::make_unique<Tappy<TcpBridgeServerConfig>>();
  config->set_publisher_id(channel_uuid);
  config->get_mutable_buffer_layout().set_num_slots(num_slots);
  config->get_mutable_buffer_layout().set_message_size(sizeof(Msg));
  config->get_underlying_listen_address().set_truncate(support::local_socket_host);
  config->set_listen_port(0U);
  config->set_num_clients(max_clients);
  config->get_underlying_channel_name().set_truncate(channel_name);
  const auto schema_encoding = LoggingTraits<Tappy<support::BridgeTestMessage<message_size>>>::schema_encoding;
  const auto& schema_definition = LoggingTraits<Tappy<support::BridgeTestMessage<message_size>>>::schema_definition;
  config->set_schema_encoding(static_cast<clockwork_logging::SchemaEncoding>(schema_encoding));
  config->get_underlying_schema_definition().resize(schema_definition.size());
  std::ranges::copy(std::as_bytes(std::span{schema_definition}), config->get_mutable_schema_definition().begin());

  const auto diagnostics_state = std::make_shared<TcpBridgeDiagnosticsState>();

  auto bridge_server = TcpBridgeServer::make(
    memres,
    *config,
    channel_factory,
    jewels::memory::make_non_null_from_ref(epoll),
    diagnostics_state,
    TcpBridgeServer::Mode::unit_test);
  REQUIRE(bridge_server);
  config.reset();
  const auto listen_addr =
    jewels::networking::SocketAddress::create(std::string{support::local_socket_host}, bridge_server->listen_port());
  const auto ephemeral_addr = jewels::networking::SocketAddress::create(std::string{support::local_socket_host}, 0);

  auto tcp_client = TcpSocket::create_connect(*listen_addr, *ephemeral_addr);
  REQUIRE(tcp_client);
  REQUIRE(set_nonblocking(tcp_client->descriptor(), true));
  bridge_server->notify(epoll, bridge_server->listen_fd(), EPOLLIN);

  // Tell the publisher to accept the connection from the bridge
  publisher.notify(epoll, -1, EPOLLIN);

  const auto msg1 = support::make_random_message<message_size>();
  testing::publish(
    publisher.publisher(),
    *msg1,
    jewels::time::SyncTime{std::chrono::nanoseconds{1001L}},
    1,
    jewels::time::SyncTime{std::chrono::nanoseconds{2001}});
  auto recv_start_time = jewels::time::SteadyClock::now();
  CHECK(support::check_next_payload(tcp_client->descriptor(), 1, *msg1, 1001L, 2001L));
  auto recv_end_time = jewels::time::SteadyClock::now();
  CAPTURE((recv_end_time - recv_start_time).count());

  if (recv_end_time - recv_start_time <= max_valid_recv_time)
  {
    const auto msg2 = support::make_random_message<message_size>();
    testing::publish(
      publisher.publisher(),
      *msg2,
      jewels::time::SyncTime{std::chrono::nanoseconds{1002L}},
      2,
      jewels::time::SyncTime{std::chrono::nanoseconds{2002}});
    recv_start_time = jewels::time::SteadyClock::now();
    CHECK(support::check_next_payload(tcp_client->descriptor(), 2, *msg2, 1002L, 2002L));
    recv_end_time = jewels::time::SteadyClock::now();
  }
  CAPTURE((recv_end_time - recv_start_time).count());

  if (recv_end_time - recv_start_time <= max_valid_recv_time)
  {
    const auto msg3 = support::make_random_message<message_size>();
    testing::publish(
      publisher.publisher(),
      *msg3,
      jewels::time::SyncTime{std::chrono::nanoseconds{1003L}},
      3,
      jewels::time::SyncTime{std::chrono::nanoseconds{2003}});
    CHECK(support::check_next_payload(tcp_client->descriptor(), 3, *msg3, 1003L, 2003L));
  }
}

} // namespace clockwork::pinion
