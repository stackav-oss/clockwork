// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/process_description.hh"
#include "clockwork/logging/lite_compressor.hh"
#include "clockwork/logging/xxh3_checksum.hh"
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/detail/socket_common.hh"
#include "clockwork/pinion/detail/tcp_socket.hh"
#include "clockwork/pinion/in_memory_channel.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/tcp_bridge_common.hh"
#include "clockwork/pinion/tcp_bridge_config.hh"
#include "clockwork/pinion/tcp_bridge_server.hh"
#include "clockwork/pinion/tests/support/epoll_snooper.hh"
#include "clockwork/pinion/tests/support/pub_sub.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/networking/sock_opt.hh"
#include "jewels/networking/socket_address.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <thread>
#include <tuple>
#include <vector>

namespace clockwork::pinion
{

namespace
{

/// Default timeout for receiving from the server
constexpr auto default_recv_timeout = std::chrono::seconds(10);

/// Timeout for receiving from the server when we don't expect anything
constexpr auto short_recv_timeout = std::chrono::milliseconds(1);

template <typename Msg>
std::optional<Msg>
decompress_message(uint64_t counts_checksum, uint64_t data_checksum, std::span<const std::byte> compressed_data)
{
  clockwork_logging::LiteCompressor compressor{jewels::memory::MemoryResource{std::pmr::new_delete_resource()}};
  Msg message{};
  const auto decompress_outcome = compressor.decompress(
    counts_checksum, data_checksum, compressed_data, std::as_writable_bytes(std::span{&message, 1U}));
  if (!decompress_outcome.ok())
  {
    return std::nullopt;
  }
  return message;
}

bool receive_buffer(
  int sock,
  std::span<std::byte> recv_buffer,
  const std::function<void()>& notify_fn,
  std::chrono::nanoseconds recv_timeout = default_recv_timeout)
{
  const auto recv_deadline = jewels::time::SyncClock::now() + recv_timeout;
  while (!recv_buffer.empty())
  {
    notify_fn();
    auto recv_bytes = ::recv(sock, recv_buffer.data(), recv_buffer.size(), 0);
    if (recv_bytes < 0)
    {
      if (errno == EAGAIN && jewels::time::SyncClock::now() < recv_deadline)
      {
        std::this_thread::sleep_for(std::chrono::microseconds(1));
        continue;
      }
      return false;
    }
    if (recv_bytes == 0)
    {
      return false;
    }
    recv_buffer = recv_buffer.subspan(static_cast<size_t>(recv_bytes));
  }
  return true;
}

std::optional<TcpMessageHeader> recv_header(
  int sock, const std::function<void()>& notify_fn, std::chrono::nanoseconds recv_timeout = default_recv_timeout)
{
  TcpMessageHeader header{};
  if (!receive_buffer(sock, std::as_writable_bytes(std::span{&header, 1U}), notify_fn, recv_timeout))
  {
    return std::nullopt;
  }
  return header;
}

template <typename Msg>
std::optional<std::tuple<TcpMessageHeader, Msg, TcpMessageTail>> recv_and_unpack(
  int sock,
  const Msg& expected_message,
  const std::function<void()>& notify_fn,
  std::chrono::nanoseconds recv_timeout = default_recv_timeout)
{
  const auto maybe_header = recv_header(sock, notify_fn, recv_timeout);
  if (!maybe_header)
  {
    return std::nullopt;
  }
  CHECK(
    maybe_header->checksum ==
    clockwork_logging::compute_xxh3_checksum(std::as_bytes(std::span{&maybe_header->body, 1U})));
  CHECK(
    maybe_header->body.message_length <=
    sizeof(expected_message) + clockwork_logging::LiteCompressor::max_compression_overhead_bytes);
  std::vector<std::byte> recv_buffer(maybe_header->body.message_length + sizeof(TcpMessageTail));
  if (!receive_buffer(sock, recv_buffer, notify_fn, recv_timeout))
  {
    return std::nullopt;
  }
  TcpMessageTail tail{};
  std::memcpy(&tail, std::span{recv_buffer}.last(sizeof(TcpMessageTail)).data(), sizeof(TcpMessageTail));

  const auto decompress_result = decompress_message<Msg>(
    tail.counts_checksum, tail.data_checksum, std::span{recv_buffer}.first(maybe_header->body.message_length));
  CHECK(decompress_result);
  if (!decompress_result)
  {
    return std::nullopt;
  }

  return {{maybe_header.value(), *decompress_result, tail}};
}

bool recv_null_header(
  int sock, const std::function<void()>& notify_fn, std::chrono::nanoseconds recv_timeout = default_recv_timeout)
{
  const auto maybe_header = recv_header(sock, notify_fn, recv_timeout);
  if (!maybe_header)
  {
    return false;
  }
  CHECK(
    maybe_header->checksum ==
    clockwork_logging::compute_xxh3_checksum(std::as_bytes(std::span{&maybe_header->body, 1U})));
  CHECK(maybe_header->body.sequence_number == 0U);
  return maybe_header->checksum ==
           clockwork_logging::compute_xxh3_checksum(std::as_bytes(std::span{&maybe_header->body, 1U})) &&
         maybe_header->body.sequence_number == 0U;
}

bool send_acknowledgement(int sock, uint64_t sequence_number)
{
  auto ack_byte = static_cast<uint8_t>(sequence_number);
  auto send_bytes = ::send(sock, &ack_byte, 1U, 0);
  return send_bytes == 1;
}

template <typename Msg>
bool check_next_payload(
  int sock,
  uint64_t expected_seqno,
  const Msg& expected_message,
  int64_t expected_publish_time,
  int64_t expected_commit_time,
  const std::function<void()>& notify_fn,
  std::chrono::nanoseconds recv_timeout = default_recv_timeout)
{
  auto payload = recv_and_unpack(sock, expected_message, notify_fn, recv_timeout);
  if (!payload)
  {
    CHECK(payload);
    return false;
  }
  const auto& [header, message, tail] = *payload;
  CHECK(header.body.sequence_number == expected_seqno);
  CHECK(header.body.publish_timestamp == expected_publish_time);
  CHECK(header.body.source_commit_timestamp == expected_commit_time);
  CHECK(message == expected_message);
  return header.body.sequence_number == expected_seqno && header.body.publish_timestamp == expected_publish_time &&
         message == expected_message && header.body.source_commit_timestamp == expected_commit_time;
}

template <typename Msg>
bool check_next_payload(
  int sock,
  uint64_t expected_seqno,
  Msg expected_message,
  const std::function<void()>& notify_fn,
  std::chrono::nanoseconds recv_timeout = default_recv_timeout)
{
  auto payload = recv_and_unpack<Msg>(sock, expected_message, notify_fn, recv_timeout);
  if (!payload)
  {
    CHECK(payload);
    return false;
  }
  const auto& [header, message, tail] = *payload;
  CHECK(header.body.sequence_number == expected_seqno);
  // CHECK(message == expected_message);
  return header.body.sequence_number == expected_seqno && message == expected_message;
}

constexpr auto socket_host = std::string_view{"127.0.0.1"};

} // namespace

TEST_CASE("TcpBridgeServer | Simple Send/Recv")
{
  using Msg = uint32_t;
  constexpr size_t num_slots = 3;
  constexpr size_t max_observer = 1;

  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  InMemoryChannel<Msg, num_slots> channel(memres);
  auto publisher = channel.make_publisher(max_observer);

  EPollSnooper epoll{};

  // Create the bridge.
  constexpr size_t max_clients = 3;
  TcpBridgeServerConfigTap config{};
  config.set_publisher_id(jewels::Uuid<common::EndpointInstanceId>::random_uuid());
  config.get_mutable_buffer_layout().set_num_slots(num_slots);
  config.get_mutable_buffer_layout().set_message_size(sizeof(Msg));
  config.get_underlying_listen_address().set_truncate(socket_host);
  config.set_listen_port(0U);
  config.set_num_clients(max_clients);
  config.get_underlying_channel_name().set_truncate("test_channel");

  const auto diagnostics_counters = std::make_shared<TcpBridgeDiagnosticsCounters>();

  auto bridge_server = TcpBridgeServer::make(
    memres,
    config,
    channel.make_subscriber(),
    jewels::memory::make_non_null_from_ref(epoll),
    diagnostics_counters,
    TcpBridgeServerMode::production);
  REQUIRE(bridge_server);
  REQUIRE(publisher.add_observer(jewels::memory::make_non_null_from_ref(*bridge_server)));
  const auto listen_addr =
    jewels::networking::SocketAddress::create(std::string{socket_host}, bridge_server->listen_port());
  const auto ephemeral_addr = jewels::networking::SocketAddress::create(std::string{socket_host}, 0);

  SECTION("Connect and Send")
  {
    auto tcp_client = TcpSocket::create_connect(*listen_addr, *ephemeral_addr);
    REQUIRE(tcp_client);
    // Set non-blocking so we can fail fast if the server is busted.
    REQUIRE(set_nonblocking(tcp_client->descriptor(), true));

    // Tell the server that a a client is waiting to be accepted.
    bridge_server->notify(epoll, bridge_server->listen_fd(), EPOLLIN);
    auto client_fd = epoll.pop_fd();
    REQUIRE(client_fd);
    // client descriptor will flagged as writeable immediately after accept().
    const auto notify_fn = [&epoll, &client_fd]() { epoll.notify(*client_fd, EPOLLOUT); };

    // This should fail. The publisher hasn't produced any messages yet.
    CHECK(!recv_and_unpack<Msg>(tcp_client->descriptor(), Msg{}, notify_fn, short_recv_timeout));

    // Send some messages and check that they arrive in order
    testing::publish(
      publisher,
      0xdeadU,
      jewels::time::SyncTime{std::chrono::nanoseconds{1234L}},
      2,
      jewels::time::SyncTime{std::chrono::nanoseconds{2345L}});
    CHECK(check_next_payload(tcp_client->descriptor(), 2, 0xdeadU, 1234L, 2345L, notify_fn));
    testing::publish(
      publisher,
      0xf00dU,
      jewels::time::SyncTime{std::chrono::nanoseconds{5677L}},
      3,
      jewels::time::SyncTime{std::chrono::nanoseconds{6788L}});
    testing::publish(
      publisher,
      0xbeefU,
      jewels::time::SyncTime{std::chrono::nanoseconds{5678L}},
      4,
      jewels::time::SyncTime{std::chrono::nanoseconds{6789L}});
    CHECK(check_next_payload(tcp_client->descriptor(), 3, 0xf00dU, 5677L, 6788L, notify_fn));
    CHECK(check_next_payload(tcp_client->descriptor(), 4, 0xbeefU, 5678L, 6789L, notify_fn));

    // Should fail until more messages arrive.
    CHECK(!recv_and_unpack<Msg>(tcp_client->descriptor(), Msg{}, notify_fn, short_recv_timeout));
    CHECK(*diagnostics_counters == TcpBridgeDiagnosticsCounters{});
  }

  SECTION("Server only sends newest message after connect")
  {
    auto tcp_client = TcpSocket::create_connect(*listen_addr, *ephemeral_addr);
    REQUIRE(tcp_client);
    // Set non-blocking so we can fail fast if the server is busted.
    REQUIRE(set_nonblocking(tcp_client->descriptor(), true));

    // Send some messages before the client connects, the client should only receive the last one
    testing::publish(
      publisher,
      0xbaadU,
      jewels::time::SyncTime{std::chrono::nanoseconds{1232L}},
      0,
      jewels::time::SyncTime{std::chrono::nanoseconds{2343L}});
    testing::publish(
      publisher,
      0xbaadU,
      jewels::time::SyncTime{std::chrono::nanoseconds{1233L}},
      1,
      jewels::time::SyncTime{std::chrono::nanoseconds{2344L}});
    testing::publish(
      publisher,
      0xdeadU,
      jewels::time::SyncTime{std::chrono::nanoseconds{1234L}},
      2,
      jewels::time::SyncTime{std::chrono::nanoseconds{2345L}});

    // Tell the server that a a client is waiting to be accepted.
    bridge_server->notify(epoll, bridge_server->listen_fd(), EPOLLIN);
    auto client_fd = epoll.pop_fd();
    REQUIRE(client_fd);
    // client descriptor will flagged as writeable immediately after accept().
    const auto notify_fn = [&epoll, &client_fd]() { epoll.notify(*client_fd, EPOLLOUT); };

    CHECK(check_next_payload(tcp_client->descriptor(), 2, 0xdeadU, 1234L, 2345L, notify_fn));

    testing::publish(
      publisher,
      0xf00dU,
      jewels::time::SyncTime{std::chrono::nanoseconds{5677L}},
      3,
      jewels::time::SyncTime{std::chrono::nanoseconds{6788L}});
    testing::publish(
      publisher,
      0xbeefU,
      jewels::time::SyncTime{std::chrono::nanoseconds{5678L}},
      4,
      jewels::time::SyncTime{std::chrono::nanoseconds{6789L}});
    CHECK(check_next_payload(tcp_client->descriptor(), 3, 0xf00dU, 5677L, 6788L, notify_fn));
    CHECK(check_next_payload(tcp_client->descriptor(), 4, 0xbeefU, 5678L, 6789L, notify_fn));

    // Should fail until more messages arrive.
    CHECK(!recv_and_unpack<Msg>(tcp_client->descriptor(), Msg{}, notify_fn, short_recv_timeout));
    CHECK(*diagnostics_counters == TcpBridgeDiagnosticsCounters{});
  }

  SECTION("Fanout")
  {
    auto client1 = TcpSocket::create_connect(*listen_addr, *ephemeral_addr);
    REQUIRE(client1);
    bridge_server->notify(epoll, bridge_server->listen_fd(), EPOLLIN);
    auto client1_fd = epoll.pop_fd();
    REQUIRE(client1_fd);
    const auto notify_fn1 = [&epoll, &client1_fd]() { epoll.notify(*client1_fd, EPOLLOUT); };

    testing::publish(publisher, 1111);
    CHECK(check_next_payload(client1->descriptor(), 0, 1111U, notify_fn1));

    auto client2 = TcpSocket::create_connect(*listen_addr, *ephemeral_addr);
    REQUIRE(client2);
    bridge_server->notify(epoll, bridge_server->listen_fd(), EPOLLIN);
    auto client2_fd = epoll.pop_fd();
    REQUIRE(client2_fd);
    const auto notify_fn2 = [&epoll, &client2_fd]() { epoll.notify(*client2_fd, EPOLLOUT); };

    testing::publish(publisher, 2222);
    CHECK(check_next_payload(client1->descriptor(), 1, 2222U, notify_fn1));
    CHECK(check_next_payload(client2->descriptor(), 1, 2222U, notify_fn2));

    auto client3 = TcpSocket::create_connect(*listen_addr, *ephemeral_addr);
    REQUIRE(client3);
    bridge_server->notify(epoll, bridge_server->listen_fd(), EPOLLIN);
    auto client3_fd = epoll.pop_fd();
    REQUIRE(client3_fd);
    const auto notify_fn3 = [&epoll, &client3_fd]() { epoll.notify(*client3_fd, EPOLLOUT); };

    // This one should get discarded by the bridge.
    auto client4 = TcpSocket::create_connect(*listen_addr, *ephemeral_addr);
    REQUIRE(client4);
    bridge_server->notify(epoll, bridge_server->listen_fd(), EPOLLIN);

    testing::publish(publisher, 3333);
    CHECK(check_next_payload(client1->descriptor(), 2, 3333U, notify_fn1));
    CHECK(check_next_payload(client2->descriptor(), 2, 3333U, notify_fn2));
    CHECK(check_next_payload(client3->descriptor(), 2, 3333U, notify_fn3));
    CHECK(!recv_and_unpack<Msg>(client4->descriptor(), Msg{}, []() {}, short_recv_timeout));

    epoll.notify(*client3_fd, EPOLLRDHUP);
    epoll.remove(*client3_fd);

    // This one should work now that client3 has closed.
    auto client5 = TcpSocket::create_connect(*listen_addr, *ephemeral_addr);
    REQUIRE(client5);
    bridge_server->notify(epoll, bridge_server->listen_fd(), EPOLLIN);
    auto client5_fd = epoll.pop_fd();
    REQUIRE(client5_fd);
    const auto notify_fn5 = [&epoll, &client5_fd]() { epoll.notify(*client5_fd, EPOLLOUT); };

    testing::publish(publisher, 4444);
    CHECK(check_next_payload(client1->descriptor(), 3, 4444U, notify_fn1));
    CHECK(check_next_payload(client2->descriptor(), 3, 4444U, notify_fn2));

    CHECK(check_next_payload(client5->descriptor(), 3, 4444U, notify_fn5));
    CHECK(*diagnostics_counters == TcpBridgeDiagnosticsCounters{});
  }
}

TEST_CASE("TcpBridgeServer | Overrun")
{
  using Msg = uint32_t;
  constexpr size_t num_slots = 3;
  constexpr size_t max_observer = 1;

  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  InMemoryChannel<Msg, num_slots> channel(memres);
  auto publisher = channel.make_publisher(max_observer);

  EPollSnooper epoll{};

  // Create the bridge.
  constexpr size_t max_clients = 3;
  TcpBridgeServerConfigTap config{};
  config.set_publisher_id(jewels::Uuid<common::EndpointInstanceId>::random_uuid());
  config.get_mutable_buffer_layout().set_num_slots(num_slots);
  config.get_mutable_buffer_layout().set_message_size(sizeof(Msg));
  config.get_underlying_listen_address().set_truncate(socket_host);
  config.set_listen_port(0U);
  config.set_num_clients(max_clients);
  config.get_underlying_channel_name().set_truncate("test_channel");

  const auto diagnostics_counters = std::make_shared<TcpBridgeDiagnosticsCounters>();

  auto bridge_server = TcpBridgeServer::make(
    memres,
    config,
    channel.make_subscriber(),
    jewels::memory::make_non_null_from_ref(epoll),
    diagnostics_counters,
    TcpBridgeServerMode::overrun_test);
  REQUIRE(bridge_server);
  REQUIRE(publisher.add_observer(jewels::memory::make_non_null_from_ref(*bridge_server)));
  const auto listen_addr =
    jewels::networking::SocketAddress::create(std::string{socket_host}, bridge_server->listen_port());
  const auto ephemeral_addr = jewels::networking::SocketAddress::create(std::string{socket_host}, 0);

  auto tcp_client = TcpSocket::create_connect(*listen_addr, *ephemeral_addr);
  REQUIRE(tcp_client);
  // Set non-blocking so we can fail fast if the server is busted.
  REQUIRE(set_nonblocking(tcp_client->descriptor(), true));

  // Tell the server that a a client is waiting to be accepted.
  bridge_server->notify(epoll, bridge_server->listen_fd(), EPOLLIN);
  auto client_fd = epoll.pop_fd();
  REQUIRE(client_fd);
  // client descriptor will flagged as writeable immediately after accept().
  const auto notify_fn = [&epoll, &client_fd]() { epoll.notify(*client_fd, EPOLLOUT); };

  // This should fail. The publisher hasn't produced any messages yet.
  CHECK(!recv_and_unpack<Msg>(tcp_client->descriptor(), Msg{}, notify_fn, short_recv_timeout));

  // Send some messages and check that they arrive in order
  testing::publish(
    publisher,
    0xdeadU,
    jewels::time::SyncTime{std::chrono::nanoseconds{1234L}},
    2,
    jewels::time::SyncTime{std::chrono::nanoseconds{2345L}});
  CHECK(check_next_payload(tcp_client->descriptor(), 2, 0xdeadU, 1234L, 2345L, notify_fn));
  testing::publish(
    publisher,
    0xbeefU,
    jewels::time::SyncTime{std::chrono::nanoseconds{5678L}},
    3,
    jewels::time::SyncTime{std::chrono::nanoseconds{6789L}});
  CHECK(check_next_payload(tcp_client->descriptor(), 3, 0xbeefU, 5678L, 6789L, notify_fn));
  CHECK(!recv_and_unpack<Msg>(tcp_client->descriptor(), Msg{}, notify_fn, short_recv_timeout));

  // Overrun the buffer and check that the server jumps to the end of the buffer
  testing::publish(
    publisher,
    0xbaadU,
    jewels::time::SyncTime{std::chrono::nanoseconds{5680L}},
    4,
    jewels::time::SyncTime{std::chrono::nanoseconds{6790L}});
  testing::publish(
    publisher,
    0xbaadU,
    jewels::time::SyncTime{std::chrono::nanoseconds{5681L}},
    5,
    jewels::time::SyncTime{std::chrono::nanoseconds{6791L}});
  testing::publish(
    publisher,
    0xbaadU,
    jewels::time::SyncTime{std::chrono::nanoseconds{5682L}},
    6,
    jewels::time::SyncTime{std::chrono::nanoseconds{6792L}});
  testing::publish(
    publisher,
    0xf00dU,
    jewels::time::SyncTime{std::chrono::nanoseconds{5689L}},
    10,
    jewels::time::SyncTime{std::chrono::nanoseconds{6799L}});
  CHECK(check_next_payload(tcp_client->descriptor(), 10, 0xf00dU, 5689L, 6799L, notify_fn));

  // Should fail until more messages arrive.
  CHECK(!recv_and_unpack<Msg>(tcp_client->descriptor(), Msg{}, notify_fn, short_recv_timeout));
  CHECK(*diagnostics_counters == TcpBridgeDiagnosticsCounters{.drop_count = 3});
}

TEST_CASE("TcpBridgeServer | Send null header until acked")
{
  using Msg = uint32_t;
  constexpr size_t num_slots = 3;
  constexpr size_t max_observer = 1;

  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  InMemoryChannel<Msg, num_slots> channel(memres);
  auto publisher = channel.make_publisher(max_observer);

  EPollSnooper epoll{};

  // Create the bridge.
  constexpr size_t max_clients = 3;
  TcpBridgeServerConfigTap config{};
  config.set_publisher_id(jewels::Uuid<common::EndpointInstanceId>::random_uuid());
  config.get_mutable_buffer_layout().set_num_slots(num_slots);
  config.get_mutable_buffer_layout().set_message_size(sizeof(Msg));
  config.get_underlying_listen_address().set_truncate(socket_host);
  config.set_listen_port(0U);
  config.set_num_clients(max_clients);
  config.get_underlying_channel_name().set_truncate("test_channel");

  const auto diagnostics_counters = std::make_shared<TcpBridgeDiagnosticsCounters>();

  auto bridge_server = TcpBridgeServer::make(
    memres,
    config,
    channel.make_subscriber(),
    jewels::memory::make_non_null_from_ref(epoll),
    diagnostics_counters,
    TcpBridgeServerMode::production);
  REQUIRE(bridge_server);
  REQUIRE(publisher.add_observer(jewels::memory::make_non_null_from_ref(*bridge_server)));
  const auto listen_addr =
    jewels::networking::SocketAddress::create(std::string{socket_host}, bridge_server->listen_port());
  const auto ephemeral_addr = jewels::networking::SocketAddress::create(std::string{socket_host}, 0);

  auto tcp_client = TcpSocket::create_connect(*listen_addr, *ephemeral_addr);
  REQUIRE(tcp_client);
  // Set non-blocking so we can fail fast if the server is busted.
  REQUIRE(set_nonblocking(tcp_client->descriptor(), true));

  // Tell the server that a a client is waiting to be accepted.
  bridge_server->notify(epoll, bridge_server->listen_fd(), EPOLLIN);
  auto client_fd = epoll.pop_fd();
  REQUIRE(client_fd);
  REQUIRE(jewels::networking::set_sock_opt<jewels::networking::SockOption::tcp_nodelay>(*client_fd, 1));

  // client descriptor will flagged as writeable immediately after accept().
  const auto notify_fn = [&epoll, &client_fd]() { epoll.notify(*client_fd, EPOLLOUT); };

  // This should fail. The publisher hasn't produced any messages yet.
  CHECK(!recv_and_unpack<Msg>(tcp_client->descriptor(), Msg{}, notify_fn, short_recv_timeout));

  // Send some messages and check that they arrive in order.
  testing::publish(
    publisher,
    0xdeadU,
    jewels::time::SyncTime{std::chrono::nanoseconds{1234L}},
    2,
    jewels::time::SyncTime{std::chrono::nanoseconds{2345L}});
  CHECK(check_next_payload(tcp_client->descriptor(), 2, 0xdeadU, 1234L, 2345L, notify_fn));
  testing::publish(
    publisher,
    0xbeefU,
    jewels::time::SyncTime{std::chrono::nanoseconds{5678L}},
    4,
    jewels::time::SyncTime{std::chrono::nanoseconds{6789L}});
  CHECK(check_next_payload(tcp_client->descriptor(), 4, 0xbeefU, 5678L, 6789L, notify_fn));

  // Should fail until more messages arrive.
  CHECK(!recv_and_unpack<Msg>(tcp_client->descriptor(), Msg{}, notify_fn, short_recv_timeout));
  CHECK_FALSE(recv_null_header(tcp_client->descriptor(), notify_fn, short_recv_timeout));

  // Server should send null headers until ack is received
  CHECK(bridge_server->is_waiting_for_ack());
  bridge_server->send_null_header_if_waiting_for_ack();
  CHECK(recv_null_header(tcp_client->descriptor(), notify_fn));

  // Send an ack for the first message, server should still keep sending null headers
  CHECK(send_acknowledgement(tcp_client->descriptor(), 2));
  epoll.notify(*client_fd, EPOLLIN);
  CHECK(bridge_server->is_waiting_for_ack());
  bridge_server->send_null_header_if_waiting_for_ack();
  CHECK(recv_null_header(tcp_client->descriptor(), notify_fn));

  // Send an ack for the last message, server should stop sending null headers
  CHECK(send_acknowledgement(tcp_client->descriptor(), 4));
  const auto acked_deadline = jewels::time::SyncClock::now() + default_recv_timeout;
  while (bridge_server->is_waiting_for_ack() && jewels::time::SyncClock::now() < acked_deadline)
  {
    epoll.notify(*client_fd, EPOLLIN);
    std::this_thread::sleep_for(std::chrono::microseconds(1));
  }
  CHECK_FALSE(bridge_server->is_waiting_for_ack());
  bridge_server->send_null_header_if_waiting_for_ack();
  CHECK_FALSE(recv_null_header(tcp_client->descriptor(), notify_fn, short_recv_timeout));
  CHECK(*diagnostics_counters == TcpBridgeDiagnosticsCounters{});
}

TEST_CASE("TcpBridgeServer | Retry Failed Send")
{
  constexpr size_t message_data_size = 1024;
  using Msg = std::array<std::byte, message_data_size>;
  constexpr size_t num_slots = 2;
  constexpr size_t max_observer = 1;

  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  InMemoryChannel<Msg, num_slots> channel(memres);
  auto publisher = channel.make_publisher(max_observer);

  EPollSnooper epoll{};

  // Create the bridge.
  constexpr auto max_clients = 1;
  TcpBridgeServerConfigTap config{};
  config.set_publisher_id(jewels::Uuid<common::EndpointInstanceId>::random_uuid());
  config.get_mutable_buffer_layout().set_num_slots(num_slots);
  config.get_mutable_buffer_layout().set_message_size(sizeof(Msg));
  config.get_underlying_listen_address().set_truncate(socket_host);
  config.set_listen_port(0U);
  config.set_num_clients(max_clients);
  config.get_underlying_channel_name().set_truncate("test_channel");

  const auto diagnostics_counters = std::make_shared<TcpBridgeDiagnosticsCounters>();

  auto bridge_server = TcpBridgeServer::make(
    memres,
    config,
    channel.make_subscriber(),
    jewels::memory::make_non_null_from_ref(epoll),
    diagnostics_counters,
    TcpBridgeServerMode::production);
  REQUIRE(bridge_server);
  REQUIRE(publisher.add_observer(jewels::memory::make_non_null_from_ref(*bridge_server)));
  const auto listen_addr =
    jewels::networking::SocketAddress::create(std::string{socket_host}, bridge_server->listen_port());
  const auto ephemeral_addr = jewels::networking::SocketAddress::create(std::string{socket_host}, 0);

  auto tcp_client = TcpSocket::create_connect(*listen_addr, *ephemeral_addr);
  REQUIRE(tcp_client);
  REQUIRE(set_nonblocking(tcp_client->descriptor(), true));
  bridge_server->notify(epoll, bridge_server->listen_fd(), EPOLLIN);
  auto client_fd = epoll.pop_fd();
  REQUIRE(client_fd);
  const auto notify_fn = [&epoll, &client_fd]() { epoll.notify(*client_fd, EPOLLOUT); };

  // Make the socket buffers as small as possible. The kernel will clamp this to
  // the system minimum.
  int bufsize = 1;
  REQUIRE(::setsockopt(*client_fd, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(int)) == 0);

  Msg message1{};
  message1.fill(std::byte{0x11});
  testing::publish(publisher, message1, jewels::time::SyncTime{std::chrono::nanoseconds{1111L}});
  auto payload = recv_and_unpack<Msg>(tcp_client->descriptor(), message1, notify_fn);
  REQUIRE(payload);
  CHECK(std::get<0U>(*payload).body.publish_timestamp == 1111L);
  CHECK(std::ranges::equal(message1, std::get<1U>(*payload)));

  // Fill up the send buffer with garbage.
  std::array<std::byte, 4096> garbage_buffer{};
  size_t total_sent = 0;
  while (true)
  {
    const auto sent = ::send(*client_fd, garbage_buffer.data(), garbage_buffer.size(), 0);
    if (sent == -1)
    {
      CHECK(errno == EAGAIN);
      break;
    }
    total_sent += static_cast<size_t>(sent);
  }

  Msg message2{};
  message2.fill(std::byte{0x22});
  Msg message3{};
  message3.fill(std::byte{0x33});
  testing::publish(publisher, message2, jewels::time::SyncTime{std::chrono::nanoseconds{2222L}});
  testing::publish(publisher, message3, jewels::time::SyncTime{std::chrono::nanoseconds{3333L}});

  // Drain the buffer.
  while (total_sent > 0)
  {
    auto recvd =
      ::recv(tcp_client->descriptor(), garbage_buffer.data(), std::min(garbage_buffer.size(), total_sent), 0);
    if (recvd == -1)
    {
      CHECK(errno == EAGAIN);
      std::this_thread::sleep_for(std::chrono::microseconds(1));
      continue;
    }
    total_sent -= static_cast<size_t>(recvd);
  }
  CHECK(total_sent == 0);

  // The bridge should have failed to send the messages published above.
  REQUIRE_FALSE(recv_and_unpack<Msg>(tcp_client->descriptor(), message2, []() {}, short_recv_timeout));

  SECTION("Normal Case")
  {
    // When the socket becomes writeable again, the bridge should retry.
    payload = recv_and_unpack<Msg>(tcp_client->descriptor(), message2, notify_fn);
    REQUIRE(payload);
    CHECK(std::get<0U>(*payload).body.publish_timestamp == 2222L);
    CHECK(std::ranges::equal(std::as_bytes(std::span{&message2, 1U}), std::get<1U>(*payload)));

    payload = recv_and_unpack<Msg>(tcp_client->descriptor(), message3, notify_fn);
    REQUIRE(payload);
    CHECK(std::get<0U>(*payload).body.publish_timestamp == 3333L);
    CHECK(std::ranges::equal(std::as_bytes(std::span{&message3, 1U}), std::get<1U>(*payload)));

    epoll.notify(*client_fd, EPOLLOUT);
    CHECK_FALSE(recv_and_unpack<Msg>(tcp_client->descriptor(), message2, notify_fn, short_recv_timeout));
    CHECK(*diagnostics_counters == TcpBridgeDiagnosticsCounters{});
  }
}

TEST_CASE("TcpBridgeServer | Retry Partial Send")
{
  constexpr size_t message_data_size = 1U << 17U;
  using Msg = std::array<std::byte, message_data_size>;
  constexpr size_t num_slots = 3;
  constexpr size_t max_observer = 1;

  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  InMemoryChannel<Msg, num_slots> channel(memres);
  auto publisher = channel.make_publisher(max_observer);

  EPollSnooper epoll{};

  // Create the bridge.
  constexpr auto max_clients = 1;
  TcpBridgeServerConfigTap config{};
  config.set_publisher_id(jewels::Uuid<common::EndpointInstanceId>::random_uuid());
  config.get_mutable_buffer_layout().set_num_slots(num_slots);
  config.get_mutable_buffer_layout().set_message_size(sizeof(Msg));
  config.get_underlying_listen_address().set_truncate(socket_host);
  config.set_listen_port(0U);
  config.set_num_clients(max_clients);
  config.get_underlying_channel_name().set_truncate("test_channel");

  const auto diagnostics_counters = std::make_shared<TcpBridgeDiagnosticsCounters>();

  auto bridge_server = TcpBridgeServer::make(
    memres,
    config,
    channel.make_subscriber(),
    jewels::memory::make_non_null_from_ref(epoll),
    diagnostics_counters,
    TcpBridgeServerMode::production);
  REQUIRE(bridge_server);
  REQUIRE(publisher.add_observer(jewels::memory::make_non_null_from_ref(*bridge_server)));
  const auto listen_addr =
    jewels::networking::SocketAddress::create(std::string{socket_host}, bridge_server->listen_port());
  const auto ephemeral_addr = jewels::networking::SocketAddress::create(std::string{socket_host}, 0);

  auto tcp_client = TcpSocket::create_connect(*listen_addr, *ephemeral_addr);
  REQUIRE(tcp_client);
  REQUIRE(set_nonblocking(tcp_client->descriptor(), true));

  bridge_server->notify(epoll, bridge_server->listen_fd(), EPOLLIN);
  auto client_fd = epoll.pop_fd();
  REQUIRE(client_fd);
  const auto notify_fn = [&epoll, &client_fd]() { epoll.notify(*client_fd, EPOLLOUT); };

  // Make the socket buffers as small as possible. The kernel will clamp this to
  // the system minimum.
  int bufsize = 1;
  REQUIRE(::setsockopt(*client_fd, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(int)) == 0);

  Msg message{};
  message.fill(std::byte{0x99});
  testing::publish(publisher, message, jewels::time::SyncTime{std::chrono::nanoseconds{1111L}});

  auto payload = recv_and_unpack<Msg>(tcp_client->descriptor(), message, notify_fn);
  REQUIRE(payload);
  CHECK(std::get<0U>(*payload).body.publish_timestamp == 1111L);
  CHECK(std::ranges::equal(message, std::get<1U>(*payload)));
  CHECK(*diagnostics_counters == TcpBridgeDiagnosticsCounters{});
}

} // namespace clockwork::pinion
