// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/process_description.hh"
#include "clockwork/common/tests/support/fake_epoll_mananger.hh"
#include "clockwork/logging/lite_compressor.hh"
#include "clockwork/logging/onboard/types.hh"
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/detail/socket_common.hh"
#include "clockwork/pinion/detail/tcp_socket.hh"
#include "clockwork/pinion/in_memory_channel.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "clockwork/pinion/tcp_bridge_client.hh"
#include "clockwork/pinion/tcp_bridge_common.hh"
#include "clockwork/pinion/tcp_bridge_config.hh"
#include "clockwork/pinion/tests/support/pub_sub.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/networking/sock_opt.hh"
#include "jewels/networking/socket_address.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <arpa/inet.h>
#include <boost/iterator/iterator_facade.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <netinet/in.h>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace clockwork::pinion
{

namespace
{

constexpr auto socket_host = std::string_view{"127.0.0.1"};

std::vector<std::byte> compress_message(std::span<const std::byte> message)
{
  clockwork_logging::LiteCompressor compressor{jewels::memory::MemoryResource{std::pmr::new_delete_resource()}};
  const auto compressed_data = compressor.compress(message);
  const auto message_size = clockwork_logging::onboard::data_spans_size(compressed_data);
  std::vector<std::byte> buffer(message_size);
  clockwork_logging::onboard::copy_data_spans(compressed_data, std::span{buffer.data(), buffer.size()});
  return buffer;
}

void send_header(int sock, uint64_t seqno, uint64_t message_len, int64_t publish_timestamp, int64_t commit_timestamp)
{
  TcpMessageHeader header{
    .sequence_number = seqno,
    .message_length = message_len,
    .publish_timestamp = publish_timestamp,
    .source_commit_timestamp = commit_timestamp};
  struct iovec iovec{};
  iovec.iov_base = &header;
  iovec.iov_len = sizeof(header);
  const size_t payload_size = sizeof(header);
  const struct msghdr msg{
    .msg_name = nullptr,
    .msg_namelen = 0,
    .msg_iov = &iovec,
    .msg_iovlen = 1,
    .msg_control = nullptr,
    .msg_controllen = 0,
    .msg_flags = 0,
  };
  REQUIRE(::sendmsg(sock, &msg, 0) == static_cast<ssize_t>(payload_size));
}

template <typename Msg>
void send_payload(
  int sock,
  uint64_t seqno,
  uint64_t message_len,
  int64_t publish_timestamp,
  int64_t commit_timestamp,
  void* message,
  TcpMessageTail* tail)
{
  std::vector<std::byte> compressed_message;
  if (message != nullptr)
  {
    compressed_message = compress_message(std::span{static_cast<const std::byte*>(message), message_len});
  }
  TcpMessageHeader header{
    .sequence_number = seqno,
    .message_length = compressed_message.size(),
    .publish_timestamp = publish_timestamp,
    .source_commit_timestamp = commit_timestamp};
  std::array<struct iovec, 3> iovecs{};
  iovecs[0].iov_base = &header;
  iovecs[0].iov_len = sizeof(header);
  iovecs[1].iov_base = compressed_message.data();
  iovecs[1].iov_len = compressed_message.size();
  iovecs[2].iov_base = tail;
  iovecs[2].iov_len = tail == nullptr ? 0 : sizeof(TcpMessageTail);
  const size_t payload_size = iovecs[0].iov_len + iovecs[1].iov_len + iovecs[2].iov_len;
  const struct msghdr msg{
    .msg_name = nullptr,
    .msg_namelen = 0,
    .msg_iov = iovecs.data(),
    .msg_iovlen = iovecs.size(),
    .msg_control = nullptr,
    .msg_controllen = 0,
    .msg_flags = 0,
  };
  REQUIRE(::sendmsg(sock, &msg, 0) == static_cast<ssize_t>(payload_size));
}

template <typename Msg>
void send_invalid_payload(
  int sock,
  uint64_t seqno,
  uint64_t message_len,
  int64_t publish_timestamp,
  int64_t commit_timestamp,
  void* message,
  TcpMessageTail* tail)
{
  std::vector<std::byte> compressed_message;
  if (message != nullptr)
  {
    compressed_message = compress_message(std::span{static_cast<const std::byte*>(message), message_len});
  }
  TcpMessageHeader header{
    .sequence_number = seqno,
    .message_length = compressed_message.size() - 4U,
    .publish_timestamp = publish_timestamp,
    .source_commit_timestamp = commit_timestamp};
  std::array<struct iovec, 3> iovecs{};
  iovecs[0].iov_base = &header;
  iovecs[0].iov_len = sizeof(header);
  iovecs[1].iov_base = compressed_message.data();
  iovecs[1].iov_len = compressed_message.size() - 4U;
  iovecs[2].iov_base = tail;
  iovecs[2].iov_len = tail == nullptr ? 0 : sizeof(TcpMessageTail);
  const size_t payload_size = iovecs[0].iov_len + iovecs[1].iov_len + iovecs[2].iov_len;
  const struct msghdr msg{
    .msg_name = nullptr,
    .msg_namelen = 0,
    .msg_iov = iovecs.data(),
    .msg_iovlen = iovecs.size(),
    .msg_control = nullptr,
    .msg_controllen = 0,
    .msg_flags = 0,
  };
  REQUIRE(::sendmsg(sock, &msg, 0) == static_cast<ssize_t>(payload_size));
}

template <typename Msg>
void send_payload(int sock, uint64_t seqno, int64_t publish_timestamp, int64_t commit_timestamp, Msg message)
{
  TcpMessageTail tail{.commit = true};
  send_payload<Msg>(sock, seqno, sizeof(Msg), publish_timestamp, commit_timestamp, &message, &tail);
}

void recv_acknowledgement(int32_t sock, uint64_t seqno)
{
  uint8_t ack_byte{};
  REQUIRE(::recv(sock, &ack_byte, 1U, 0) == 1);
  REQUIRE(ack_byte == static_cast<uint8_t>(seqno));
}

std::pair<std::unique_ptr<TcpSocket>, jewels::networking::SocketAddress> make_listen_socket()
{
  constexpr auto backlog = 1;
  const auto ephemeral_addr = jewels::networking::SocketAddress::create(std::string{socket_host}, 0);
  auto listen_socket = TcpSocket::create_listen(*ephemeral_addr, backlog);
  REQUIRE(listen_socket);
  ::sockaddr_in actual_addr{};
  socklen_t addr_size = sizeof(actual_addr);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) For socket API
  REQUIRE(::getsockname(listen_socket->descriptor(), reinterpret_cast<::sockaddr*>(&actual_addr), &addr_size) == 0);
  const auto listen_addr =
    jewels::networking::SocketAddress::create(std::string{socket_host}, ::ntohs(actual_addr.sin_port));
  REQUIRE(listen_addr);
  return {std::make_unique<TcpSocket>(*std::move(listen_socket)), *listen_addr};
}

std::unique_ptr<TcpSocket> make_listen_socket(const jewels::networking::SocketAddress& listen_addr)
{
  constexpr auto backlog = 1;
  auto listen_socket = TcpSocket::create_listen(listen_addr, backlog);
  REQUIRE(listen_socket);
  return std::make_unique<TcpSocket>(*std::move(listen_socket));
}

} // namespace

TEST_CASE("TcpBridgeClient | Receive")
{
  using Msg = uint32_t;
  constexpr size_t num_slots = 3;
  constexpr size_t max_observer = 1;

  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  InMemoryChannel<Msg, num_slots> channel(memres);
  auto subscriber = channel.make_subscriber();
  testing::FakeEPollManager epoll;
  auto [listen_socket, listen_addr] = make_listen_socket();

  TcpBridgeClientConfigTap config{};
  config.get_underlying_channel_name().set_truncate("test_channel");
  config.get_mutable_publisher_endpoint().set_publisher_id(jewels::Uuid<common::EndpointInstanceId>::random_uuid());
  config.get_underlying_server_address().set_truncate(socket_host);
  config.set_server_port(::ntohs(listen_addr.port()));

  // Create the bridge.
  const auto bridge_client = TcpBridgeClient::make(
    memres,
    config,
    channel.make_publisher(max_observer),
    channel.make_subscriber(),
    jewels::memory::make_non_null_from_ref(epoll),
    std::make_shared<TcpBridgeDiagnosticsCounters>());
  REQUIRE(bridge_client);

  const int accepted = ::accept(listen_socket->descriptor(), nullptr, nullptr);
  REQUIRE(accepted >= 0);
  REQUIRE(set_nonblocking(accepted, true));
  REQUIRE(jewels::networking::set_sock_opt<jewels::networking::SockOption::tcp_nodelay>(accepted, 1));

  SECTION("Simple Receive and Publish")
  {
    std::vector<Msg> expected{1111U, 2222U, 3333U};
    std::vector<int64_t> expected_publish_stamps{4444L, 5555L, 6666L};
    std::vector<int64_t> expected_commit_stamps{5555L, 6666L, 7777L};
    std::vector<uint64_t> expected_seqnos{444UL, 555UL, 666UL};
    for (size_t i = 0; i < expected.size(); ++i)
    {
      send_payload(accepted, expected_seqnos[i], expected_publish_stamps[i], expected_commit_stamps[i], expected[i]);
    }
    bridge_client->notify(epoll, bridge_client->socket_fd(), EPOLLIN);
    for (size_t i = 0; i < expected.size(); ++i)
    {
      recv_acknowledgement(accepted, expected_seqnos[i]);
    }
    CHECK(testing::dump<Msg>(subscriber) == expected);
    auto messages = subscriber.available();
    for (size_t i = 0; i < messages.size(); ++i)
    {
      CHECK(messages[static_cast<int64_t>(i)].header()->publish_timestamp == expected_publish_stamps[i]);
      CHECK(messages[static_cast<int64_t>(i)].header()->sequence_number == expected_seqnos[i]);
      CHECK(messages[static_cast<int64_t>(i)].header()->source_commit_timestamp == expected_commit_stamps[i]);
    }
  }

  SECTION("Incremental Receive")
  {
    constexpr size_t chunk_size = 2U;
    std::vector<Msg> expected{0xdeadbeefU, 0xc000ffee, 0xbaadf00d};
    std::vector<int64_t> expected_publish_stamps{1234L, 5678L, 9012L};
    std::vector<int64_t> expected_commit_stamps{2345L, 6789L, 1234L};
    std::vector<uint64_t> expected_seqnos{22U, 23U, 24U};
    auto expected_span = std::span(expected.data(), expected.size());
    for (size_t i = 0; i < expected.size(); i++)
    {
      CAPTURE(i);
      const auto compressed_message = compress_message(std::as_bytes(expected_span.subspan(i, 1)));
      const auto msg_span = std::span{compressed_message.data(), compressed_message.size()};
      // First, send over just the header.
      send_header(accepted, expected_seqnos[i], msg_span.size(), expected_publish_stamps[i], expected_commit_stamps[i]);
      bridge_client->notify(epoll, bridge_client->socket_fd(), EPOLLIN);
      CHECK(subscriber.available().size() == i);

      // Pretend the message got delivered in two chunks for some reason.
      REQUIRE(static_cast<size_t>(::send(accepted, msg_span.first(chunk_size).data(), chunk_size, 0)) == chunk_size);
      bridge_client->notify(epoll, bridge_client->socket_fd(), EPOLLIN);
      CHECK(subscriber.available().size() == i);
      REQUIRE(chunk_size == 2U);
      REQUIRE(
        static_cast<size_t>(::send(accepted, msg_span.subspan(chunk_size).data(), msg_span.size() - chunk_size, 0)) ==
        msg_span.size() - chunk_size);
      bridge_client->notify(epoll, bridge_client->socket_fd(), EPOLLIN);
      CHECK(subscriber.available().size() == i);

      // Send the tail.
      TcpMessageTail tail{.commit = true};
      REQUIRE(::send(accepted, &tail, sizeof(tail), 0) == sizeof(tail));
      bridge_client->notify(epoll, bridge_client->socket_fd(), EPOLLIN);
      recv_acknowledgement(accepted, expected_seqnos[i]);
      CHECK(subscriber.available().size() == i + 1);
    }
    CHECK(testing::dump<Msg>(subscriber) == expected);
    auto messages = subscriber.available();
    for (size_t i = 0; i < messages.size(); ++i)
    {
      CHECK(messages[static_cast<int64_t>(i)].header()->publish_timestamp == expected_publish_stamps[i]);
      CHECK(messages[static_cast<int64_t>(i)].header()->sequence_number == expected_seqnos[i]);
      CHECK(messages[static_cast<int64_t>(i)].header()->source_commit_timestamp == expected_commit_stamps[i]);
    }
  }

  SECTION("Invalid Message")
  {
    Msg msg{0xbadU};
    TcpMessageTail tail{.commit = true};
    send_invalid_payload<Msg>(accepted, 0, sizeof(Msg), 0L, 0L, &msg, &tail);
    bridge_client->notify(epoll, bridge_client->socket_fd(), EPOLLIN);
    // The client should have detected the message size mismatch, discarded any
    // pending writes to the channel, and closed the socket.
    CHECK(subscriber.available().empty());
    CHECK(::recv(accepted, &msg, 4, 0) == 0);
  }

  SECTION("Commit Flag")
  {
    Msg msg{0xbadbeefU};
    TcpMessageTail tail{.commit = false};
    send_payload<Msg>(accepted, 0, sizeof(Msg), 0L, 0L, &msg, &tail);
    bridge_client->notify(epoll, bridge_client->socket_fd(), EPOLLIN);
    recv_acknowledgement(accepted, 0);
    // The client should have seen that the server said to abort and discarded
    // the pending write to the channel.
    CHECK(subscriber.available().empty());

    // Socket should still be OK, though.
    REQUIRE(::recv(accepted, &msg, 4, 0) == -1);

    // Valid messages should proceed just fine afterwards.
    tail.commit = true;
    send_payload<Msg>(accepted, 1, sizeof(Msg), 0L, 0L, &msg, &tail);
    bridge_client->notify(epoll, bridge_client->socket_fd(), EPOLLIN);
    recv_acknowledgement(accepted, 1);
    CHECK(subscriber.available().size() == 1);
  }
}

TEST_CASE("TcpBridgeClient | Reconnect")
{
  using Msg = uint32_t;
  constexpr size_t num_slots = 8;
  constexpr size_t max_observer = 1;

  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  InMemoryChannel<Msg, num_slots> channel(memres);
  auto subscriber = channel.make_subscriber();
  testing::FakeEPollManager epoll;
  auto [listen_socket, listen_addr] = make_listen_socket();

  TcpBridgeClientConfigTap config{};
  config.get_underlying_channel_name().set_truncate("test_channel");
  config.get_mutable_publisher_endpoint().set_publisher_id(jewels::Uuid<common::EndpointInstanceId>::random_uuid());
  config.get_underlying_server_address().set_truncate(socket_host);
  config.set_server_port(::ntohs(listen_addr.port()));

  // Create the bridge.
  auto bridge_client = TcpBridgeClient::make(
    memres,
    config,
    channel.make_publisher(max_observer),
    subscriber,
    jewels::memory::make_non_null_from_ref(epoll),
    std::make_shared<TcpBridgeDiagnosticsCounters>());
  REQUIRE(bridge_client);

  auto accepted = ::accept(listen_socket->descriptor(), nullptr, nullptr);
  REQUIRE(accepted >= 0);
  REQUIRE(set_nonblocking(accepted, true));
  REQUIRE(jewels::networking::set_sock_opt<jewels::networking::SockOption::tcp_nodelay>(accepted, 1));

  std::vector<Msg> expected1{1111U, 2222U, 3333U};
  std::vector<int64_t> expected_publish_stamps1{4444L, 5555L, 6666L};
  std::vector<int64_t> expected_commit_stamps1{5555L, 6666L, 7777L};
  std::vector<uint64_t> expected_seqnos1{4UL, 5UL, 6UL};
  for (size_t i = 0; i < expected1.size(); ++i)
  {
    send_payload(accepted, expected_seqnos1[i], expected_publish_stamps1[i], expected_commit_stamps1[i], expected1[i]);
  }
  bridge_client->notify(epoll, bridge_client->socket_fd(), EPOLLIN);
  for (size_t i = 0; i < expected1.size(); ++i)
  {
    recv_acknowledgement(accepted, expected_seqnos1[i]);
  }
  CHECK(testing::dump<Msg>(subscriber) == expected1);
  auto messages = subscriber.available();
  for (size_t i = 0; i < messages.size(); ++i)
  {
    CHECK(messages[static_cast<int64_t>(i)].header()->publish_timestamp == expected_publish_stamps1[i]);
    CHECK(messages[static_cast<int64_t>(i)].header()->sequence_number == expected_seqnos1[i]);
    CHECK(messages[static_cast<int64_t>(i)].header()->source_commit_timestamp == expected_commit_stamps1[i]);
  }

  ::close(accepted);
  listen_socket.reset();

  bridge_client->notify(epoll, bridge_client->socket_fd(), EPOLLIN);
  REQUIRE(bridge_client->socket_fd() == -1);
  std::this_thread::sleep_for(std::chrono::seconds(2));
  bridge_client->notify(epoll, bridge_client->timer_fd(), EPOLLIN);
  REQUIRE(bridge_client->socket_fd() != -1);
  bridge_client->notify(epoll, bridge_client->socket_fd(), EPOLLOUT);
  REQUIRE(bridge_client->socket_fd() == -1);

  listen_socket = make_listen_socket(listen_addr);

  std::this_thread::sleep_for(std::chrono::seconds(2));
  bridge_client->notify(epoll, bridge_client->timer_fd(), EPOLLIN);
  REQUIRE(bridge_client->socket_fd() != -1);
  bridge_client->notify(epoll, bridge_client->socket_fd(), EPOLLOUT);
  REQUIRE(bridge_client->socket_fd() != -1);

  accepted = ::accept(listen_socket->descriptor(), nullptr, nullptr);
  REQUIRE(accepted >= 0);
  REQUIRE(set_nonblocking(accepted, true));
  REQUIRE(jewels::networking::set_sock_opt<jewels::networking::SockOption::tcp_nodelay>(accepted, 1));

  std::vector<Msg> expected2{1111U, 2222U, 3333U, 4444U};
  std::vector<int64_t> expected_publish_stamps2{4444L, 5555L, 6666L, 7777U};
  std::vector<int64_t> expected_commit_stamps2{5555L, 6666L, 7777L, 8888L};
  std::vector<uint64_t> expected_seqnos2{4UL, 5UL, 6UL, 7L};
  for (size_t i = 0; i < expected2.size(); ++i)
  {
    send_payload(accepted, expected_seqnos2[i], expected_publish_stamps2[i], expected_commit_stamps2[i], expected2[i]);
  }
  bridge_client->notify(epoll, bridge_client->socket_fd(), EPOLLIN);
  for (size_t i = 0; i < expected2.size(); ++i)
  {
    recv_acknowledgement(accepted, expected_seqnos2[i]);
  }
  CHECK(testing::dump<Msg>(subscriber) == expected2);
  messages = subscriber.available();
  for (size_t i = 0; i < messages.size(); ++i)
  {
    CHECK(messages[static_cast<int64_t>(i)].header()->publish_timestamp == expected_publish_stamps2[i]);
    CHECK(messages[static_cast<int64_t>(i)].header()->sequence_number == expected_seqnos2[i]);
    CHECK(messages[static_cast<int64_t>(i)].header()->source_commit_timestamp == expected_commit_stamps2[i]);
  }

  epoll.remove(bridge_client->socket_fd());
  epoll.remove(bridge_client->timer_fd());
  bridge_client.reset();
  ::close(accepted);

  bridge_client = TcpBridgeClient::make(
    memres,
    config,
    channel.make_publisher(max_observer),
    subscriber,
    jewels::memory::make_non_null_from_ref(epoll),
    std::make_shared<TcpBridgeDiagnosticsCounters>());
  REQUIRE(bridge_client);

  accepted = ::accept(listen_socket->descriptor(), nullptr, nullptr);
  REQUIRE(accepted >= 0);
  REQUIRE(set_nonblocking(accepted, true));
  REQUIRE(jewels::networking::set_sock_opt<jewels::networking::SockOption::tcp_nodelay>(accepted, 1));

  std::vector<Msg> expected3{1111U, 2222U, 3333U, 4444U, 5555U};
  std::vector<int64_t> expected_publish_stamps3{4444L, 5555L, 6666L, 7777U, 8888U};
  std::vector<int64_t> expected_commit_stamps3{5555L, 6666L, 7777L, 8888L, 9999L};
  std::vector<uint64_t> expected_seqnos3{4UL, 5UL, 6UL, 7L, 8L};
  for (size_t i = 0; i < expected3.size(); ++i)
  {
    send_payload(accepted, expected_seqnos3[i], expected_publish_stamps3[i], expected_commit_stamps3[i], expected3[i]);
  }
  bridge_client->notify(epoll, bridge_client->socket_fd(), EPOLLIN);
  for (size_t i = 0; i < expected2.size(); ++i)
  {
    recv_acknowledgement(accepted, expected_seqnos3[i]);
  }
  CHECK(testing::dump<Msg>(subscriber) == expected3);
  messages = subscriber.available();
  for (size_t i = 0; i < messages.size(); ++i)
  {
    CHECK(messages[static_cast<int64_t>(i)].header()->publish_timestamp == expected_publish_stamps3[i]);
    CHECK(messages[static_cast<int64_t>(i)].header()->sequence_number == expected_seqnos3[i]);
    CHECK(messages[static_cast<int64_t>(i)].header()->source_commit_timestamp == expected_commit_stamps3[i]);
  }
}

} // namespace clockwork::pinion
