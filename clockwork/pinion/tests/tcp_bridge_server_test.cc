// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/process_description.hh"
#include "clockwork/logging/lite_compressor.hh"
#include "clockwork/logging/onboard/types.hh"
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
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <tuple>
#include <vector>

namespace clockwork::pinion
{

namespace
{

template <typename Msg>
std::vector<std::byte> compress_message(const Msg& message)
{
  clockwork_logging::LiteCompressor compressor{jewels::memory::MemoryResource{std::pmr::new_delete_resource()}};
  const auto compressed_data = compressor.compress(std::as_bytes(std::span{&message, 1U}));
  const auto message_size = clockwork_logging::onboard::data_spans_size(compressed_data);
  std::vector<std::byte> buffer(message_size);
  clockwork_logging::onboard::copy_data_spans(compressed_data, std::span{buffer.data(), buffer.size()});
  return buffer;
}

template <typename Msg>
std::optional<Msg> decompress_message(std::span<const std::byte> compressed_data)
{
  clockwork_logging::LiteCompressor compressor{jewels::memory::MemoryResource{std::pmr::new_delete_resource()}};
  Msg message{};
  const auto decompress_result =
    compressor.decompress(compressed_data, std::as_writable_bytes(std::span{&message, 1U}));
  if (!decompress_result)
  {
    return std::nullopt;
  }
  return message;
}

template <typename Msg>
std::optional<std::tuple<TcpMessageHeader, uint32_t, TcpMessageTail>>
recv_and_unpack(int sock, const Msg& expected_message)
{
  const auto message_size = compress_message(expected_message).size();
  const auto payload_size = sizeof(TcpMessageHeader) + message_size + sizeof(TcpMessageTail);

  TcpMessageHeader header{};
  TcpMessageTail tail{};
  std::vector<std::byte> recv_buffer(message_size);
  std::array<struct iovec, 3> iovecs{};
  iovecs[0].iov_base = &header;
  iovecs[0].iov_len = sizeof(header);
  iovecs[1].iov_base = recv_buffer.data();
  iovecs[1].iov_len = recv_buffer.size();
  iovecs[2].iov_base = &tail;
  iovecs[2].iov_len = sizeof(tail);
  struct msghdr msg{
    .msg_name = nullptr,
    .msg_namelen = 0,
    .msg_iov = iovecs.data(),
    .msg_iovlen = iovecs.size(),
    .msg_control = nullptr,
    .msg_controllen = 0,
    .msg_flags = 0,
  };
  auto recv_bytes = ::recvmsg(sock, &msg, 0);
  if (static_cast<size_t>(recv_bytes) != payload_size)
  {
    return std::nullopt;
  }

  const auto decompress_result = decompress_message<Msg>(std::span{recv_buffer.data(), recv_buffer.size()});
  CHECK(decompress_result);
  if (!decompress_result)
  {
    return std::nullopt;
  }

  return {{header, *decompress_result, tail}};
}

bool recv_null_header(int sock)
{
  TcpMessageHeader header{};
  std::array<struct iovec, 1> iovecs{};
  iovecs[0].iov_base = &header;
  iovecs[0].iov_len = sizeof(header);
  struct msghdr msg{
    .msg_name = nullptr,
    .msg_namelen = 0,
    .msg_iov = iovecs.data(),
    .msg_iovlen = iovecs.size(),
    .msg_control = nullptr,
    .msg_controllen = 0,
    .msg_flags = 0,
  };
  auto recv_bytes = ::recvmsg(sock, &msg, 0);
  if (static_cast<size_t>(recv_bytes) != sizeof(header))
  {
    return false;
  }
  CHECK(header.sequence_number == 0U);
  return header.sequence_number == 0U;
}

bool send_acknowledgement(int sock, uint64_t sequence_number)
{
  auto ack_byte = static_cast<uint8_t>(sequence_number);
  auto send_bytes = ::send(sock, &ack_byte, 1U, 0);
  return send_bytes == 1;
}

template <typename Msg>
bool check_next_payload(
  int sock, uint64_t expected_seqno, Msg expected_message, int64_t expected_publish_time, int64_t expected_commit_time)
{
  auto payload = recv_and_unpack(sock, expected_message);
  if (!payload)
  {
    return false;
  }
  const auto& [header, message, tail] = *payload;
  CHECK(tail.commit);
  return header.message_length == compress_message(expected_message).size() &&
         header.sequence_number == expected_seqno && header.publish_timestamp == expected_publish_time &&
         message == expected_message && header.source_commit_timestamp == expected_commit_time;
}

template <typename Msg>
bool check_next_payload(int sock, uint64_t expected_seqno, Msg expected_message)
{
  auto payload = recv_and_unpack<Msg>(sock, expected_message);
  if (!payload)
  {
    return false;
  }
  const auto& [header, message, tail] = *payload;
  CHECK(tail.commit);
  CHECK(header.sequence_number == expected_seqno);
  return header.message_length == compress_message(expected_message).size() &&
         header.sequence_number == expected_seqno && message == expected_message;
}

// returns false if the socket isn't ready or if it takes too long to receive
// everything.
template <size_t size>
bool recv_all_iovecs(int sock, const std::array<struct iovec, size>& iovecs)
{
  auto local_iovecs = iovecs;
  auto deadline = jewels::time::SteadyClock::now() + std::chrono::seconds(1);
  struct msghdr msg{
    .msg_name = nullptr,
    .msg_namelen = 0,
    .msg_iov = local_iovecs.data(),
    .msg_iovlen = local_iovecs.size(),
    .msg_control = nullptr,
    .msg_controllen = 0,
    .msg_flags = 0,
  };
  while (jewels::time::SteadyClock::now() < deadline)
  {
    auto bytes_received = recvmsg(sock, &msg, 0);
    if (bytes_received <= 0)
    {
      return false;
    }
    advance_iovecs(std::span(local_iovecs), static_cast<size_t>(bytes_received));
    if (std::ranges::all_of(std::span(local_iovecs), [](const auto& iov) { return iov.iov_len == 0; }))
    {
      return true;
    }
  }
  return false;
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

  auto bridge_server = TcpBridgeServer::make(
    memres,
    config,
    channel.make_subscriber(),
    jewels::memory::make_non_null_from_ref(epoll),
    std::make_shared<TcpBridgeDiagnosticsCounters>());
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
    epoll.notify(*client_fd, EPOLLOUT);

    // This should fail. The publisher hasn't produced any messages yet.
    CHECK(!recv_and_unpack<Msg>(tcp_client->descriptor(), Msg{}));

    // Send some messages and check that they arrive in order.
    testing::publish(
      publisher,
      0xdeadU,
      jewels::time::SyncTime{std::chrono::nanoseconds{1234L}},
      2,
      jewels::time::SyncTime{std::chrono::nanoseconds{2345L}});
    CHECK(check_next_payload(tcp_client->descriptor(), 2, 0xdeadU, 1234L, 2345L));
    testing::publish(
      publisher,
      0xbeefU,
      jewels::time::SyncTime{std::chrono::nanoseconds{5678L}},
      4,
      jewels::time::SyncTime{std::chrono::nanoseconds{6789L}});
    CHECK(check_next_payload(tcp_client->descriptor(), 4, 0xbeefU, 5678L, 6789L));

    // Should fail until more messages arrive.
    CHECK(!recv_and_unpack<Msg>(tcp_client->descriptor(), Msg{}));
  }

  SECTION("Fanout")
  {
    auto client1 = TcpSocket::create_connect(*listen_addr, *ephemeral_addr);
    REQUIRE(client1);
    bridge_server->notify(epoll, bridge_server->listen_fd(), EPOLLIN);

    testing::publish(publisher, 1111);
    CHECK(check_next_payload(client1->descriptor(), 0, 1111U));

    auto client2 = TcpSocket::create_connect(*listen_addr, *ephemeral_addr);
    REQUIRE(client2);
    bridge_server->notify(epoll, bridge_server->listen_fd(), EPOLLIN);

    testing::publish(publisher, 2222);
    CHECK(check_next_payload(client1->descriptor(), 1, 2222U));
    CHECK(check_next_payload(client2->descriptor(), 0, 1111U));
    CHECK(check_next_payload(client2->descriptor(), 1, 2222U));

    {
      auto client3 = TcpSocket::create_connect(*listen_addr, *ephemeral_addr);
      REQUIRE(client3);
      bridge_server->notify(epoll, bridge_server->listen_fd(), EPOLLIN);

      // This one should get discarded by the bridge.
      auto client4 = TcpSocket::create_connect(*listen_addr, *ephemeral_addr);
      REQUIRE(client4);
      bridge_server->notify(epoll, bridge_server->listen_fd(), EPOLLIN);

      testing::publish(publisher, 3333);
      CHECK(check_next_payload(client1->descriptor(), 2, 3333U));
      CHECK(check_next_payload(client2->descriptor(), 2, 3333U));
      CHECK(!recv_and_unpack<Msg>(client4->descriptor(), Msg{}));
    }

    auto client3_fd = epoll.pop_fd();
    REQUIRE(client3_fd);
    epoll.notify(*client3_fd, EPOLLRDHUP);
    epoll.remove(*client3_fd);

    // This one should work now that client3 has closed.
    auto client5 = TcpSocket::create_connect(*listen_addr, *ephemeral_addr);
    REQUIRE(client5);
    bridge_server->notify(epoll, bridge_server->listen_fd(), EPOLLIN);

    testing::publish(publisher, 4444);
    CHECK(check_next_payload(client1->descriptor(), 3, 4444U));
    CHECK(check_next_payload(client2->descriptor(), 3, 4444U));

    CHECK(check_next_payload(client5->descriptor(), 1, 2222U));
    CHECK(check_next_payload(client5->descriptor(), 2, 3333U));
    CHECK(check_next_payload(client5->descriptor(), 3, 4444U));
  }
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

  auto bridge_server = TcpBridgeServer::make(
    memres,
    config,
    channel.make_subscriber(),
    jewels::memory::make_non_null_from_ref(epoll),
    std::make_shared<TcpBridgeDiagnosticsCounters>());
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
  epoll.notify(*client_fd, EPOLLOUT);

  // This should fail. The publisher hasn't produced any messages yet.
  CHECK(!recv_and_unpack<Msg>(tcp_client->descriptor(), Msg{}));

  // Send some messages and check that they arrive in order.
  testing::publish(
    publisher,
    0xdeadU,
    jewels::time::SyncTime{std::chrono::nanoseconds{1234L}},
    2,
    jewels::time::SyncTime{std::chrono::nanoseconds{2345L}});
  CHECK(check_next_payload(tcp_client->descriptor(), 2, 0xdeadU, 1234L, 2345L));
  testing::publish(
    publisher,
    0xbeefU,
    jewels::time::SyncTime{std::chrono::nanoseconds{5678L}},
    4,
    jewels::time::SyncTime{std::chrono::nanoseconds{6789L}});
  CHECK(check_next_payload(tcp_client->descriptor(), 4, 0xbeefU, 5678L, 6789L));

  // Should fail until more messages arrive.
  CHECK(!recv_and_unpack<Msg>(tcp_client->descriptor(), Msg{}));
  CHECK_FALSE(recv_null_header(tcp_client->descriptor()));

  // Server should send null headers until ack is received
  bridge_server->send_null_header_if_waiting_for_ack();
  CHECK(recv_null_header(tcp_client->descriptor()));

  // Send an ack for the first message, server should still keep sending null headers
  CHECK(send_acknowledgement(tcp_client->descriptor(), 2));
  epoll.notify(*client_fd, EPOLLIN);
  bridge_server->send_null_header_if_waiting_for_ack();
  CHECK(recv_null_header(tcp_client->descriptor()));

  // Send an ack for the last message, server should stop sending null headers
  CHECK(send_acknowledgement(tcp_client->descriptor(), 4));
  epoll.notify(*client_fd, EPOLLIN);
  bridge_server->send_null_header_if_waiting_for_ack();
  CHECK_FALSE(recv_null_header(tcp_client->descriptor()));
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

  auto bridge_server = TcpBridgeServer::make(
    memres,
    config,
    channel.make_subscriber(),
    jewels::memory::make_non_null_from_ref(epoll),
    std::make_shared<TcpBridgeDiagnosticsCounters>());
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

  // Make the socket buffers as small as possible. The kernel will clamp this to
  // the system minimum.
  int bufsize = 1;
  REQUIRE(::setsockopt(*client_fd, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(int)) == 0);

  Msg message1{};
  message1.fill(std::byte{0x11});
  testing::publish(publisher, message1);
  const auto compressed_message1 = compress_message(message1);
  const auto payload_size1 = sizeof(TcpMessageHeader) + compressed_message1.size() + sizeof(TcpMessageTail);
  std::vector<std::byte> buffer1(payload_size1);
  CHECK(static_cast<size_t>(::recv(tcp_client->descriptor(), buffer1.data(), payload_size1, 0)) == payload_size1);
  CHECK(std::ranges::equal(
    compressed_message1,
    std::span(buffer1.data(), buffer1.size()).subspan(sizeof(TcpMessageHeader), compressed_message1.size())));

  // Fill up the send buffer with garbage.
  size_t total_sent = 0;
  while (true)
  {
    auto sent = ::send(*client_fd, buffer1.data(), buffer1.size(), 0);
    if (sent == -1)
    {
      CHECK(errno == EAGAIN);
      break;
    }
    total_sent += static_cast<size_t>(sent);
  }

  Msg message2{};
  message2.fill(std::byte{0x22});
  const auto compressed_message2 = compress_message(message2);
  std::vector<std::byte> buffer2(compressed_message2.size());
  Msg message3{};
  message3.fill(std::byte{0x33});
  const auto compressed_message3 = compress_message(message3);
  std::vector<std::byte> buffer3(compressed_message3.size());
  testing::publish(publisher, message2, jewels::time::SyncTime{std::chrono::nanoseconds{2222L}});
  testing::publish(publisher, message3, jewels::time::SyncTime{std::chrono::nanoseconds{3333L}});

  // Drain the buffer.
  while (total_sent > 0)
  {
    auto recvd = ::recv(tcp_client->descriptor(), buffer1.data(), std::min(buffer1.size(), total_sent), 0);
    if (recvd == -1)
    {
      CHECK(errno == EAGAIN);
      break;
    }
    total_sent -= static_cast<size_t>(recvd);
  }
  CHECK(total_sent == 0);

  // The bridge should have failed to send the messages published above.
  REQUIRE(::recv(tcp_client->descriptor(), buffer2.data(), compressed_message2.size(), 0) == -1);

  TcpMessageHeader header{};
  TcpMessageTail tail{};
  std::array<struct iovec, 3> iovecs{};
  iovecs[0].iov_base = &header;
  iovecs[0].iov_len = sizeof(header);
  iovecs[2].iov_base = &tail;
  iovecs[2].iov_len = sizeof(tail);

  SECTION("Normal Case")
  {
    // When the socket becomes writeable again, the bridge should retry.
    epoll.notify(*client_fd, EPOLLOUT);
    iovecs[1].iov_base = buffer2.data();
    iovecs[1].iov_len = compressed_message2.size();
    CHECK(recv_all_iovecs(tcp_client->descriptor(), iovecs));
    CHECK(header.publish_timestamp == 2222L);
    CHECK(tail.commit);
    CHECK(std::ranges::equal(compressed_message2, buffer2));

    iovecs[1].iov_base = buffer3.data();
    iovecs[1].iov_len = compressed_message3.size();
    CHECK(recv_all_iovecs(tcp_client->descriptor(), iovecs));
    CHECK(header.publish_timestamp == 3333L);
    CHECK(tail.commit);
    CHECK(std::ranges::equal(compressed_message3, buffer3));

    CHECK(::recv(tcp_client->descriptor(), buffer1.data(), payload_size1, 0) == -1);
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

  auto bridge_server = TcpBridgeServer::make(
    memres,
    config,
    channel.make_subscriber(),
    jewels::memory::make_non_null_from_ref(epoll),
    std::make_shared<TcpBridgeDiagnosticsCounters>());
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

  // Make the socket buffers as small as possible. The kernel will clamp this to
  // the system minimum.
  int bufsize = 1;
  REQUIRE(::setsockopt(*client_fd, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(int)) == 0);

  Msg message{};
  message.fill(std::byte{0x99});
  testing::publish(publisher, message);

  const auto compressed_message = compress_message(message);
  const auto payload_size = sizeof(TcpMessageHeader) + compressed_message.size();
  std::vector<std::byte> buffer(payload_size);
  auto span = std::span(buffer);
  auto remaining = payload_size;
  auto recv_bytes = ::recv(tcp_client->descriptor(), span.data(), remaining, 0);
  CHECK(recv_bytes > 0);
  CHECK(recv_bytes < static_cast<ssize_t>(payload_size));
  remaining -= static_cast<size_t>(recv_bytes);

  // At this point the client shouldn't receive anything until the bridge has
  // been told the socket is ready.
  CHECK(::recv(tcp_client->descriptor(), span.subspan(payload_size - remaining).data(), remaining, 0) == -1);

  while (remaining > 0)
  {
    epoll.notify(*client_fd, EPOLLOUT);
    recv_bytes = ::recv(tcp_client->descriptor(), span.subspan(payload_size - remaining).data(), remaining, 0);
    REQUIRE(recv_bytes > 0);
    remaining -= static_cast<size_t>(recv_bytes);
  }
  CHECK(remaining == 0);
  CHECK(std::ranges::equal(compressed_message, span.last(compressed_message.size())));
  CHECK(buffer.back() == std::byte{0x99});
}

} // namespace clockwork::pinion
