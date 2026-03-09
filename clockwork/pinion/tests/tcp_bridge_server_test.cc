// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/detail/socket_common.hh"
#include "clockwork/pinion/detail/tcp_socket.hh"
#include "clockwork/pinion/in_memory_channel.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/tcp_bridge_common.hh"
#include "clockwork/pinion/tcp_bridge_config_clk_cc.hh"
#include "clockwork/pinion/tcp_bridge_server.hh"
#include "clockwork/pinion/tests/support/bridge_test_support.hh"
#include "clockwork/pinion/tests/support/epoll_snooper.hh"
#include "clockwork/pinion/tests/support/pub_sub.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/container/compare.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/math/constants.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/networking/socket_address.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/fix_catch2_cerr_nonthreadsafe_redirect.hh" // IWYU pragma: keep
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <array>
#include <chrono>
#include <compare>
#include <cstddef>
#include <memory>
#include <memory_resource>
#include <ratio>
#include <string>
#include <sys/epoll.h>
#include <thread>
#include <unordered_set>

namespace clockwork::pinion
{

TEST_CASE("TcpBridgeServer | Simple Send/Recv")
{
  const auto is_bulk_data = GENERATE(false, true);
  CAPTURE(is_bulk_data);
  constexpr auto message_size = jewels::math::constants::bytes_per_kb<size_t> + 1U;
  using Msg = std::array<std::byte, message_size>;
  constexpr size_t num_slots = 3;
  constexpr size_t max_observer = 1;

  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  InMemoryChannel<Msg, num_slots, false> channel(memres);
  auto publisher = channel.make_publisher(max_observer);

  EPollSnooper epoll{};

  // Create the bridge.
  constexpr size_t max_clients = 3;
  Tappy<TcpBridgeServerConfig> config{};
  config.set_publisher_id(jewels::Uuid<common::EndpointInstanceId>::random_uuid());
  config.get_mutable_buffer_layout().set_num_slots(num_slots);
  config.get_mutable_buffer_layout().set_message_size(sizeof(Msg));
  config.get_underlying_listen_address().set_truncate(support::local_socket_host);
  config.set_listen_port(0U);
  config.set_num_clients(max_clients);
  config.get_underlying_channel_name().set_truncate("test_channel");
  config.set_is_bulk_data(is_bulk_data);

  const auto diagnostics_state = std::make_shared<TcpBridgeDiagnosticsState>();

  auto bridge_server = TcpBridgeServer::make(
    memres, config, channel.make_subscriber(), jewels::memory::make_non_null_from_ref(epoll), diagnostics_state);
  REQUIRE(bridge_server);
  REQUIRE(publisher.add_observer(jewels::memory::make_non_null_from_ref(*bridge_server)));
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

    const auto start_time = jewels::time::SteadyClock::now();

    // Send some messages and check that they arrive in order
    const auto msg2 = support::make_random_message<message_size>();
    testing::publish(
      publisher,
      *msg2,
      jewels::time::SyncTime{std::chrono::nanoseconds{1234L}},
      2,
      jewels::time::SyncTime{std::chrono::nanoseconds{2345L}});
    CHECK(support::check_next_payload(tcp_client->descriptor(), 2, *msg2, 1234L, 2345L));
    const auto msg3 = support::make_random_message<message_size>();
    testing::publish(
      publisher,
      *msg3,
      jewels::time::SyncTime{std::chrono::nanoseconds{5677L}},
      3,
      jewels::time::SyncTime{std::chrono::nanoseconds{6788L}});
    const auto msg4 = support::make_random_message<message_size>();
    testing::publish(
      publisher,
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
      publisher,
      *msg0,
      jewels::time::SyncTime{std::chrono::nanoseconds{1232L}},
      0,
      jewels::time::SyncTime{std::chrono::nanoseconds{2343L}});
    const auto msg1 = support::make_random_message<message_size>();
    testing::publish(
      publisher,
      *msg1,
      jewels::time::SyncTime{std::chrono::nanoseconds{1233L}},
      1,
      jewels::time::SyncTime{std::chrono::nanoseconds{2344L}});
    const auto msg2 = support::make_random_message<message_size>();
    testing::publish(
      publisher,
      *msg2,
      jewels::time::SyncTime{std::chrono::nanoseconds{1234L}},
      2,
      jewels::time::SyncTime{std::chrono::nanoseconds{2345L}});

    // Tell the server that a a client is waiting to be accepted.
    bridge_server->notify(epoll, bridge_server->listen_fd(), EPOLLIN);

    // Server will send the first message immediately after accepting the connection
    CHECK(support::check_next_payload(tcp_client->descriptor(), 2, *msg2, 1234L, 2345L));

    const auto msg3 = support::make_random_message<message_size>();
    testing::publish(
      publisher,
      *msg3,
      jewels::time::SyncTime{std::chrono::nanoseconds{5677L}},
      3,
      jewels::time::SyncTime{std::chrono::nanoseconds{6788L}});
    const auto msg4 = support::make_random_message<message_size>();
    testing::publish(
      publisher,
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
    CHECK(support::check_num_clients(*bridge_server, 1U));
    CHECK(support::recv_null_header(client1->descriptor(), 0U));

    const auto msg0 = support::make_random_message<message_size>();
    testing::publish(publisher, *msg0);
    CHECK(support::check_next_payload(client1->descriptor(), 0, *msg0));

    const auto msg1 = support::make_random_message<message_size>();
    testing::publish(publisher, *msg1);

    auto client2 = TcpSocket::create_connect(*listen_addr, *ephemeral_addr);
    REQUIRE(client2);
    bridge_server->notify(epoll, bridge_server->listen_fd(), EPOLLIN);
    CHECK(support::check_num_clients(*bridge_server, 2U));

    CHECK(support::check_next_payload(client1->descriptor(), 1, *msg1));
    CHECK(support::check_next_payload(client2->descriptor(), 1, *msg1));

    const auto msg2 = support::make_random_message<message_size>();
    testing::publish(publisher, *msg2);

    auto client3 = TcpSocket::create_connect(*listen_addr, *ephemeral_addr);
    REQUIRE(client3);
    bridge_server->notify(epoll, bridge_server->listen_fd(), EPOLLIN);
    CHECK(support::check_num_clients(*bridge_server, 3U));

    // This one should get discarded by the bridge.
    auto client4 = TcpSocket::create_connect(*listen_addr, *ephemeral_addr);
    REQUIRE(client4);
    bridge_server->notify(epoll, bridge_server->listen_fd(), EPOLLIN);
    CHECK(support::check_num_clients(*bridge_server, 3U));

    CHECK(support::check_next_payload(client1->descriptor(), 2, *msg2));
    CHECK(support::check_next_payload(client2->descriptor(), 2, *msg2));
    CHECK(support::check_next_payload(client3->descriptor(), 2, *msg2));

    CHECK(client3->release_descriptor().close());
    CHECK(support::check_num_clients(*bridge_server, 2U));

    const auto msg3 = support::make_random_message<message_size>();
    testing::publish(publisher, *msg3);

    CHECK(support::check_next_payload(client1->descriptor(), 3, *msg3));
    CHECK(support::check_next_payload(client2->descriptor(), 3, *msg3));

    // This one should work now that client3 has closed.
    auto client5 = TcpSocket::create_connect(*listen_addr, *ephemeral_addr);
    REQUIRE(client5);
    bridge_server->notify(epoll, bridge_server->listen_fd(), EPOLLIN);
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
  constexpr auto message_size = jewels::math::constants::bytes_per_kb<size_t>;
  using Msg = std::array<std::byte, message_size>;
  constexpr size_t num_slots = 3;
  constexpr size_t max_observer = 1;

  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  InMemoryChannel<Msg, num_slots, false> channel(memres);
  auto publisher = channel.make_publisher(max_observer);

  EPollSnooper epoll{};

  // Create the bridge.
  constexpr size_t max_clients = 3;
  Tappy<TcpBridgeServerConfig> config{};
  config.set_publisher_id(jewels::Uuid<common::EndpointInstanceId>::random_uuid());
  config.get_mutable_buffer_layout().set_num_slots(num_slots);
  config.get_mutable_buffer_layout().set_message_size(sizeof(Msg));
  config.get_underlying_listen_address().set_truncate(support::local_socket_host);
  config.set_listen_port(0U);
  config.set_num_clients(max_clients);
  config.get_underlying_channel_name().set_truncate("test_channel");

  const auto diagnostics_state = std::make_shared<TcpBridgeDiagnosticsState>();

  auto bridge_server = TcpBridgeServer::make(
    memres,
    config,
    channel.make_subscriber(),
    jewels::memory::make_non_null_from_ref(epoll),
    diagnostics_state,
    TcpBridgeServer::Mode::overrun_test);
  REQUIRE(bridge_server);
  REQUIRE(publisher.add_observer(jewels::memory::make_non_null_from_ref(*bridge_server)));
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

  // Send some messages and check that they arrive in order
  const auto msg1 = support::make_random_message<message_size>();
  testing::publish(
    publisher,
    *msg1,
    jewels::time::SyncTime{std::chrono::nanoseconds{1234L}},
    1,
    jewels::time::SyncTime{std::chrono::nanoseconds{2345L}});
  const auto msg2 = support::make_random_message<message_size>();
  testing::publish(
    publisher,
    *msg2,
    jewels::time::SyncTime{std::chrono::nanoseconds{5678L}},
    2,
    jewels::time::SyncTime{std::chrono::nanoseconds{6789L}});
  bridge_server->forced_notify();
  CHECK(support::check_next_payload(tcp_client->descriptor(), 1, *msg1, 1234L, 2345L));
  CHECK(support::check_next_payload(tcp_client->descriptor(), 2, *msg2, 5678L, 6789L));

  // Overrun the buffer and check that the server jumps to the end of the buffer
  const auto msg3 = support::make_random_message<message_size>();
  testing::publish(
    publisher,
    *msg3,
    jewels::time::SyncTime{std::chrono::nanoseconds{5680L}},
    3,
    jewels::time::SyncTime{std::chrono::nanoseconds{6790L}});
  std::this_thread::sleep_for(std::chrono::milliseconds(250));
  const auto msg4 = support::make_random_message<message_size>();
  testing::publish(
    publisher,
    *msg4,
    jewels::time::SyncTime{std::chrono::nanoseconds{5681L}},
    4,
    jewels::time::SyncTime{std::chrono::nanoseconds{6791L}});
  const auto msg5 = support::make_random_message<message_size>();
  testing::publish(
    publisher,
    *msg5,
    jewels::time::SyncTime{std::chrono::nanoseconds{5682L}},
    5,
    jewels::time::SyncTime{std::chrono::nanoseconds{6792L}});
  const auto msg6 = support::make_random_message<message_size>();
  testing::publish(
    publisher,
    *msg6,
    jewels::time::SyncTime{std::chrono::nanoseconds{5683L}},
    6,
    jewels::time::SyncTime{std::chrono::nanoseconds{6793L}});

  bridge_server->forced_notify();
  CHECK(support::check_next_payload(tcp_client->descriptor(), 6, *msg6, 5683L, 6793L));

  const auto diagnostics_counters = diagnostics_state->get_and_reset_counters();
  CHECK(support::compare_diagnostics_counters(diagnostics_counters, TcpBridgeDiagnosticsCounters{.drop_count = 3U}));
}

TEST_CASE("TcpBridgeServer | Send null header until acked and send keep-alives periodically")
{
  constexpr auto message_size = jewels::math::constants::bytes_per_kb<size_t>;
  using Msg = std::array<std::byte, message_size>;
  constexpr size_t num_slots = 3;
  constexpr size_t max_observer = 1;

  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  InMemoryChannel<Msg, num_slots, false> channel(memres);
  auto publisher = channel.make_publisher(max_observer);

  EPollSnooper epoll{};

  // Create the bridge.
  constexpr size_t max_clients = 3;
  Tappy<TcpBridgeServerConfig> config{};
  config.set_publisher_id(jewels::Uuid<common::EndpointInstanceId>::random_uuid());
  config.get_mutable_buffer_layout().set_num_slots(num_slots);
  config.get_mutable_buffer_layout().set_message_size(sizeof(Msg));
  config.get_underlying_listen_address().set_truncate(support::local_socket_host);
  config.set_listen_port(0U);
  config.set_num_clients(max_clients);
  config.get_underlying_channel_name().set_truncate("test_channel");

  const auto diagnostics_state = std::make_shared<TcpBridgeDiagnosticsState>();

  auto bridge_server = TcpBridgeServer::make(
    memres, config, channel.make_subscriber(), jewels::memory::make_non_null_from_ref(epoll), diagnostics_state);
  REQUIRE(bridge_server);
  REQUIRE(publisher.add_observer(jewels::memory::make_non_null_from_ref(*bridge_server)));
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

  // Send some messages and check that they arrive in order.
  const auto msg2 = support::make_random_message<message_size>();
  testing::publish(
    publisher,
    *msg2,
    jewels::time::SyncTime{std::chrono::nanoseconds{1234L}},
    2,
    jewels::time::SyncTime{std::chrono::nanoseconds{2345L}});
  CHECK(support::check_next_payload(tcp_client->descriptor(), 2, *msg2, 1234L, 2345L));
  const auto msg4 = support::make_random_message<message_size>();
  testing::publish(
    publisher,
    *msg4,
    jewels::time::SyncTime{std::chrono::nanoseconds{5678L}},
    4,
    jewels::time::SyncTime{std::chrono::nanoseconds{6789L}});
  CHECK(support::check_next_payload(tcp_client->descriptor(), 4, *msg4, 5678L, 6789L));

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

TEST_CASE("TcpBridgeServer | Large messages")
{
  constexpr auto message_size = 32U * jewels::math::constants::bytes_per_mb<size_t>;
  using Msg = std::array<std::byte, message_size>;
  constexpr size_t num_slots = 2;
  constexpr size_t max_observer = 1;

  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  InMemoryChannel<Msg, num_slots, false> channel(memres);
  auto publisher = channel.make_publisher(max_observer);

  EPollSnooper epoll{};

  // Create the bridge.
  constexpr auto max_clients = 1;
  Tappy<TcpBridgeServerConfig> config{};
  config.set_publisher_id(jewels::Uuid<common::EndpointInstanceId>::random_uuid());
  config.get_mutable_buffer_layout().set_num_slots(num_slots);
  config.get_mutable_buffer_layout().set_message_size(sizeof(Msg));
  config.get_underlying_listen_address().set_truncate(support::local_socket_host);
  config.set_listen_port(0U);
  config.set_num_clients(max_clients);
  config.get_underlying_channel_name().set_truncate("test_channel");

  const auto diagnostics_state = std::make_shared<TcpBridgeDiagnosticsState>();

  auto bridge_server = TcpBridgeServer::make(
    memres, config, channel.make_subscriber(), jewels::memory::make_non_null_from_ref(epoll), diagnostics_state);
  REQUIRE(bridge_server);
  REQUIRE(publisher.add_observer(jewels::memory::make_non_null_from_ref(*bridge_server)));
  const auto listen_addr =
    jewels::networking::SocketAddress::create(std::string{support::local_socket_host}, bridge_server->listen_port());
  const auto ephemeral_addr = jewels::networking::SocketAddress::create(std::string{support::local_socket_host}, 0);

  auto tcp_client = TcpSocket::create_connect(*listen_addr, *ephemeral_addr);
  REQUIRE(tcp_client);
  REQUIRE(set_nonblocking(tcp_client->descriptor(), true));
  bridge_server->notify(epoll, bridge_server->listen_fd(), EPOLLIN);

  const auto msg1 = support::make_random_message<message_size>();
  testing::publish(
    publisher,
    *msg1,
    jewels::time::SyncTime{std::chrono::nanoseconds{1001L}},
    1,
    jewels::time::SyncTime{std::chrono::nanoseconds{2001}});
  CHECK(support::check_next_payload(tcp_client->descriptor(), 1, *msg1, 1001L, 2001L));

  const auto msg2 = support::make_random_message<message_size>();
  testing::publish(
    publisher,
    *msg2,
    jewels::time::SyncTime{std::chrono::nanoseconds{1002L}},
    2,
    jewels::time::SyncTime{std::chrono::nanoseconds{2002}});
  const auto msg3 = support::make_random_message<message_size>();
  testing::publish(
    publisher,
    *msg3,
    jewels::time::SyncTime{std::chrono::nanoseconds{1003L}},
    3,
    jewels::time::SyncTime{std::chrono::nanoseconds{2003}});
  CHECK(support::check_next_payload(tcp_client->descriptor(), 2, *msg2, 1002L, 2002L));
  CHECK(support::check_next_payload(tcp_client->descriptor(), 3, *msg3, 1003L, 2003L));
}

} // namespace clockwork::pinion
