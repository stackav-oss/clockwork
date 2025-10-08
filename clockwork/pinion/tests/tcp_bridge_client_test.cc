// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/process_description.hh"
#include "clockwork/common/tests/support/fake_epoll_mananger.hh"
#include "clockwork/logging/lite_compressor.hh"
#include "clockwork/logging/onboard/types.hh"
#include "clockwork/logging/xxh3_checksum.hh"
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
#include "jewels/callsig/outparam.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/networking/sock_opt.hh"
#include "jewels/networking/socket_address.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <arpa/inet.h>
#include <boost/iterator/iterator_facade.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

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

using jewels::Out;

/// Timeout for receiving from the client
constexpr auto recv_timeout = std::chrono::seconds(5);

constexpr auto socket_host = std::string_view{"127.0.0.1"};

void compress_message(
  Out<std::vector<std::byte>> compressed_message,
  Out<uint64_t> counts_checksum,
  Out<uint64_t> data_checksum,
  std::span<const std::byte> message)
{
  clockwork_logging::LiteCompressor compressor{jewels::memory::MemoryResource{std::pmr::new_delete_resource()}};
  std::span<const std::span<const std::byte>> compressed_spans;
  compressor.compress(Out{compressed_spans}, Out(*counts_checksum), Out(*data_checksum), message);
  const auto message_size = clockwork_logging::onboard::data_spans_size(compressed_spans);
  compressed_message->resize(message_size);
  clockwork_logging::onboard::copy_data_spans(compressed_spans, std::span{*compressed_message});
}

void send_header(int sock, uint64_t seqno, uint64_t message_len, int64_t publish_timestamp, int64_t commit_timestamp)
{
  TcpMessageHeader header{
    .body = TcpMessageHeaderBody{
      .sequence_number = seqno,
      .message_length = message_len,
      .publish_timestamp = publish_timestamp,
      .source_commit_timestamp = commit_timestamp,
    }};
  header.checksum = clockwork_logging::compute_xxh3_checksum(std::as_bytes(std::span{&header.body, 1U}));
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
void send_payload(int sock, uint64_t seqno, int64_t publish_timestamp, int64_t commit_timestamp, const Msg& message)
{
  TcpMessageTail tail{};
  std::vector<std::byte> compressed_message;
  compress_message(
    Out{compressed_message},
    Out{tail.counts_checksum},
    Out{tail.data_checksum},
    std::as_bytes(std::span{&message, 1U}));
  TcpMessageHeader header{
    .body = TcpMessageHeaderBody{
      .sequence_number = seqno,
      .message_length = compressed_message.size(),
      .publish_timestamp = publish_timestamp,
      .source_commit_timestamp = commit_timestamp}};
  header.checksum = clockwork_logging::compute_xxh3_checksum(std::as_bytes(std::span{&header.body, 1U}));
  std::array<struct iovec, 3> iovecs{};
  iovecs[0].iov_base = &header;
  iovecs[0].iov_len = sizeof(header);
  iovecs[1].iov_base = compressed_message.data();
  iovecs[1].iov_len = compressed_message.size();
  iovecs[2].iov_base = &tail;
  iovecs[2].iov_len = sizeof(TcpMessageTail);
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

/// Type of corruption to send in corrupted payload
enum class CorruptionType : uint8_t
{
  corrupt_size,
  corrupt_header_checksum,
  corrupt_counts,
  corrupt_data
};

template <typename Msg>
void send_corrupted_payload(
  int sock,
  uint64_t seqno,
  int64_t publish_timestamp,
  int64_t commit_timestamp,
  const Msg& message,
  CorruptionType corruption_type)
{
  TcpMessageTail tail{};
  std::vector<std::byte> compressed_message;
  compress_message(
    Out{compressed_message},
    Out{tail.counts_checksum},
    Out{tail.data_checksum},
    std::as_bytes(std::span{&message, 1U}));
  if (corruption_type == CorruptionType::corrupt_counts)
  {
    ++tail.counts_checksum;
  }
  if (corruption_type == CorruptionType::corrupt_data)
  {
    ++tail.data_checksum;
  }
  TcpMessageHeader header{
    .body = TcpMessageHeaderBody{
      .sequence_number = seqno,
      .message_length = compressed_message.size(),
      .publish_timestamp = publish_timestamp,
      .source_commit_timestamp = commit_timestamp}};
  if (corruption_type == CorruptionType::corrupt_size)
  {
    header.body.message_length = sizeof(Msg) + clockwork_logging::LiteCompressor::max_compression_overhead_bytes + 1U;
  }
  header.checksum = clockwork_logging::compute_xxh3_checksum(std::as_bytes(std::span{&header.body, 1U}));
  if (corruption_type == CorruptionType::corrupt_header_checksum)
  {
    ++header.checksum;
  }
  std::array<struct iovec, 3> iovecs{};
  iovecs[0].iov_base = &header;
  iovecs[0].iov_len = sizeof(header);
  iovecs[1].iov_base = compressed_message.data();
  iovecs[1].iov_len = compressed_message.size();
  iovecs[2].iov_base = &tail;
  iovecs[2].iov_len = sizeof(TcpMessageTail);
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

void send_partial_header(int sock, size_t bytes_to_send)
{
  std::vector<std::byte> buffer(bytes_to_send);
  const auto magic_bytes = std::min(sizeof(tcp_message_header_magic_number.size()), bytes_to_send);
  std::memcpy(buffer.data(), tcp_message_header_magic_number.data(), magic_bytes);
  if (bytes_to_send > tcp_message_header_magic_number.size())
  {
    const auto fill_bytes = bytes_to_send - tcp_message_header_magic_number.size();
    std::memset(&buffer.at(tcp_message_header_magic_number.size()), 0, fill_bytes);
  }
  REQUIRE(::send(sock, buffer.data(), buffer.size(), 0) == static_cast<ssize_t>(buffer.size()));
}

template <typename Msg>
void send_extended_payload(
  int sock,
  uint64_t seqno,
  uint64_t message_len,
  int64_t publish_timestamp,
  int64_t commit_timestamp,
  void* /*message*/,
  TcpMessageTail* tail)
{
  std::vector<std::byte> payload(message_len + clockwork_logging::LiteCompressor::max_compression_overhead_bytes + 1U);
  TcpMessageHeader header{
    .body = TcpMessageHeaderBody{
      .sequence_number = seqno,
      .message_length = payload.size(),
      .publish_timestamp = publish_timestamp,
      .source_commit_timestamp = commit_timestamp}};
  header.checksum = clockwork_logging::compute_xxh3_checksum(std::as_bytes(std::span{&header.body, 1U}));
  std::array<struct iovec, 3> iovecs{};
  iovecs[0].iov_base = &header;
  iovecs[0].iov_len = sizeof(header);
  iovecs[1].iov_base = payload.data();
  iovecs[1].iov_len = payload.size();
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

bool socket_is_closed(int32_t sock, const std::function<void()>& notify_fn)
{
  const auto recv_deadline = jewels::time::SyncClock::now() + recv_timeout;
  uint8_t datum{};
  while (true)
  {
    notify_fn();
    const auto bytes_recvd = ::recv(sock, &datum, 1U, 0);
    if (bytes_recvd == -1 && errno == EAGAIN && jewels::time::SyncClock::now() < recv_deadline)
    {
      std::this_thread::sleep_for(std::chrono::microseconds(1));
      continue;
    }
    if (bytes_recvd == -1)
    {
      CHECK(errno == ECONNRESET);
      if (errno != ECONNRESET)
      {
        return false;
      }
      continue;
    }
    CHECK(bytes_recvd == 0);
    return bytes_recvd == 0;
  }
}

[[nodiscard]] int32_t accept_connection(const TcpSocket& listen_socket, const std::function<void()>& notify_fn)
{
  const auto accept_deadline = jewels::time::SyncClock::now() + recv_timeout;
  while (true)
  {
    notify_fn();
    const auto accepted = ::accept(listen_socket.descriptor(), nullptr, nullptr);
    if (accepted < 0 && errno == EAGAIN && jewels::time::SyncClock::now() < accept_deadline)
    {
      std::this_thread::sleep_for(std::chrono::microseconds(1));
      continue;
    }
    return accepted;
  }
}

bool recv_acknowledgement(int32_t sock, uint64_t seqno, const std::function<void()>& notify_fn)
{
  const auto recv_deadline = jewels::time::SyncClock::now() + recv_timeout;
  uint8_t ack_byte{};
  while (true)
  {
    notify_fn();
    const auto bytes_recvd = ::recv(sock, &ack_byte, 1U, 0);
    if (bytes_recvd == -1 && errno == EAGAIN && jewels::time::SyncClock::now() < recv_deadline)
    {
      std::this_thread::sleep_for(std::chrono::microseconds(1));
      continue;
    }
    CHECK(bytes_recvd == 1);
    if (bytes_recvd != 1)
    {
      return false;
    }
    break;
  }
  CHECK(ack_byte == static_cast<uint8_t>(seqno));
  return ack_byte == static_cast<uint8_t>(seqno);
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
  REQUIRE(set_nonblocking(listen_socket->descriptor(), true));
  return {std::make_unique<TcpSocket>(*std::move(listen_socket)), *listen_addr};
}

std::unique_ptr<TcpSocket> make_listen_socket(const jewels::networking::SocketAddress& listen_addr)
{
  constexpr auto backlog = 1;
  auto listen_socket = TcpSocket::create_listen(listen_addr, backlog);
  REQUIRE(listen_socket);
  REQUIRE(set_nonblocking(listen_socket->descriptor(), true));
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
  config.get_mutable_publisher_endpoint().set_publisher_id(jewels::Uuid<common::EndpointInstanceId>::random_uuid());
  config.get_mutable_publisher_endpoint().get_underlying_channel_name().set_truncate("test_channel");
  config.get_underlying_server_address().set_truncate(socket_host);
  config.set_server_port(::ntohs(listen_addr.port()));

  const auto diagnostics_counters = std::make_shared<TcpBridgeDiagnosticsCounters>();

  // Create the bridge.
  const auto bridge_client = TcpBridgeClient::make(
    memres,
    config,
    channel.make_publisher(max_observer),
    channel.make_subscriber(),
    jewels::memory::make_non_null_from_ref(epoll),
    diagnostics_counters);
  REQUIRE(bridge_client);

  const auto accepted = accept_connection(
    *listen_socket, [&bridge_client, &epoll]() { bridge_client->notify(epoll, bridge_client->timer_fd(), EPOLLIN); });
  REQUIRE(accepted >= 0);
  REQUIRE(set_nonblocking(accepted, true));
  REQUIRE(jewels::networking::set_sock_opt<jewels::networking::SockOption::tcp_nodelay>(accepted, 1));

  SECTION("Simple Receive and Publish")
  {
    const std::vector<Msg> expected{1111U, 2222U, 3333U};
    const std::vector<int64_t> expected_publish_stamps{4444L, 5555L, 6666L};
    const std::vector<int64_t> expected_commit_stamps{5555L, 6666L, 7777L};
    const std::vector<uint64_t> expected_seqnos{444UL, 555UL, 666UL};
    for (size_t i = 0; i < expected.size(); ++i)
    {
      send_payload(accepted, expected_seqnos[i], expected_publish_stamps[i], expected_commit_stamps[i], expected[i]);
      CHECK(recv_acknowledgement(
        accepted,
        expected_seqnos[i],
        [&bridge_client, &epoll]() { bridge_client->notify(epoll, bridge_client->socket_fd(), EPOLLIN); }));
    }
    CHECK(testing::dump<Msg>(subscriber) == expected);
    auto messages = subscriber.available();
    for (size_t i = 0; i < messages.size(); ++i)
    {
      CHECK(messages[static_cast<int64_t>(i)].header()->publish_timestamp == expected_publish_stamps[i]);
      CHECK(messages[static_cast<int64_t>(i)].header()->sequence_number == expected_seqnos[i]);
      CHECK(messages[static_cast<int64_t>(i)].header()->source_commit_timestamp == expected_commit_stamps[i]);
    }
    diagnostics_counters->max_bridge_latency = {};
    diagnostics_counters->max_latency_channel_name = {};
    CHECK(*diagnostics_counters == TcpBridgeDiagnosticsCounters{});
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
      TcpMessageTail tail{};
      std::vector<std::byte> compressed_message;
      compress_message(
        Out{compressed_message},
        Out{tail.counts_checksum},
        Out{tail.data_checksum},
        std::as_bytes(expected_span.subspan(i, 1)));
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
      REQUIRE(::send(accepted, &tail, sizeof(tail), 0) == sizeof(tail));
      CHECK(recv_acknowledgement(
        accepted,
        expected_seqnos[i],
        [&bridge_client, &epoll]() { bridge_client->notify(epoll, bridge_client->socket_fd(), EPOLLIN); }));
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
    diagnostics_counters->max_bridge_latency = {};
    diagnostics_counters->max_latency_channel_name = {};
    CHECK(*diagnostics_counters == TcpBridgeDiagnosticsCounters{});
  }

  SECTION("Invalid Message - bad compression counts failure")
  {
    const Msg msg{0xbadU};
    send_corrupted_payload<Msg>(accepted, 0, 0L, 0L, msg, CorruptionType::corrupt_counts);
    // The client should have dropped the message but still sent an acknowledgement
    CHECK(recv_acknowledgement(
      accepted, 0U, [&epoll, &bridge_client]() { bridge_client->notify(epoll, bridge_client->socket_fd(), EPOLLIN); }));
    CHECK(subscriber.available().empty());
    diagnostics_counters->max_bridge_latency = {};
    diagnostics_counters->max_latency_channel_name = {};
    CHECK(*diagnostics_counters == TcpBridgeDiagnosticsCounters{.malformed_messages = 1});
  }

  SECTION("Invalid Message - bad compression data failure")
  {
    const Msg msg{0xbadU};
    send_corrupted_payload<Msg>(accepted, 0, 0L, 0L, msg, CorruptionType::corrupt_data);
    // The client should have dropped the message but still sent an acknowledgement
    CHECK(recv_acknowledgement(
      accepted, 0U, [&epoll, &bridge_client]() { bridge_client->notify(epoll, bridge_client->socket_fd(), EPOLLIN); }));
    CHECK(subscriber.available().empty());
    diagnostics_counters->max_bridge_latency = {};
    diagnostics_counters->max_latency_channel_name = {};
    CHECK(*diagnostics_counters == TcpBridgeDiagnosticsCounters{.malformed_messages = 1});
  }


  SECTION("Invalid Message - invalid message header checksum")
  {
    const Msg msg{0xbadU};

    /// Step 1: Send a message with a corrupted header checksum
    send_corrupted_payload<Msg>(accepted, 0, 0L, 0L, msg, CorruptionType::corrupt_header_checksum);

    /// Step 2: Send a partial header to test recovering from loss of framing after bad header
    const auto partial_header_size = GENERATE_REF(range(size_t{0U}, tcp_message_header_magic_number.size() * 2U));
    CAPTURE(partial_header_size);
    send_partial_header(accepted, partial_header_size);
    // The client should have detected the bad checksum and be looking for the next valid header
    const int64_t expected_publish_stamp{4444L};
    const int64_t expected_commit_stamp{5555L};
    const uint64_t expected_seqno{4UL};

    /// Step 3: Send a valid message following the corrupted header and partial payload
    send_payload(accepted, expected_seqno, expected_publish_stamp, expected_commit_stamp, msg);

    /// Step 4: Receive an acknowledgement that the client recovered and received the valid message
    CHECK(recv_acknowledgement(
      accepted,
      expected_seqno,
      [&epoll, &bridge_client]() { bridge_client->notify(epoll, bridge_client->socket_fd(), EPOLLIN); }));

    /// Step 5: Check that the client published the correct message to the pinion buffer
    CHECK(testing::dump<Msg>(subscriber) == std::vector<Msg>{msg});
    auto messages = subscriber.available();
    REQUIRE(messages.size() == 1U);
    CHECK(messages[0].header()->publish_timestamp == expected_publish_stamp);
    CHECK(messages[0].header()->sequence_number == expected_seqno);
    CHECK(messages[0].header()->source_commit_timestamp == expected_commit_stamp);

    /// Step 6: Check that the client diagnostics counters indicate that a corrupted message was received
    diagnostics_counters->max_bridge_latency = {};
    diagnostics_counters->max_latency_channel_name = {};
    CHECK(*diagnostics_counters == TcpBridgeDiagnosticsCounters{.malformed_messages = 1});
  }

  SECTION("Invalid Message - invalid message size")
  {
    const Msg msg{0xbadU};
    send_corrupted_payload<Msg>(accepted, 0, 0L, 0L, msg, CorruptionType::corrupt_size);
    // The client should have detected the message size mismatch, discarded any
    // pending writes to the channel, and closed the socket.
    CHECK(socket_is_closed(
      accepted, [&epoll, &bridge_client]() { bridge_client->notify(epoll, bridge_client->socket_fd(), EPOLLIN); }));
    CHECK(subscriber.available().empty());
    diagnostics_counters->max_bridge_latency = {};
    diagnostics_counters->max_latency_channel_name = {};
    CHECK(*diagnostics_counters == TcpBridgeDiagnosticsCounters{.closed_socket_count = 1, .malformed_messages = 1});
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
  config.get_mutable_publisher_endpoint().set_publisher_id(jewels::Uuid<common::EndpointInstanceId>::random_uuid());
  config.get_mutable_publisher_endpoint().get_underlying_channel_name().set_truncate("test_channel");
  config.get_underlying_server_address().set_truncate(socket_host);
  config.set_server_port(::ntohs(listen_addr.port()));

  auto diagnostics_counters = std::make_shared<TcpBridgeDiagnosticsCounters>();

  // Create the bridge.
  auto bridge_client = TcpBridgeClient::make(
    memres,
    config,
    channel.make_publisher(max_observer),
    subscriber,
    jewels::memory::make_non_null_from_ref(epoll),
    diagnostics_counters);
  REQUIRE(bridge_client);

  auto accepted = accept_connection(
    *listen_socket, [&bridge_client, &epoll]() { bridge_client->notify(epoll, bridge_client->timer_fd(), EPOLLIN); });
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
    CHECK(recv_acknowledgement(
      accepted,
      expected_seqnos1[i],
      [&epoll, &bridge_client]() { bridge_client->notify(epoll, bridge_client->socket_fd(), EPOLLIN); }));
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

  const auto close_deadline = jewels::time::SyncClock::now() + recv_timeout;
  while (bridge_client->socket_fd() != -1 && jewels::time::SyncClock::now() < close_deadline)
  {
    bridge_client->notify(epoll, bridge_client->socket_fd(), EPOLLIN);
    std::this_thread::sleep_for(std::chrono::microseconds(1));
  }
  REQUIRE(bridge_client->socket_fd() == -1);

  listen_socket = make_listen_socket(listen_addr);

  const auto open_deadline = jewels::time::SyncClock::now() + recv_timeout;
  while (bridge_client->socket_fd() == -1 && jewels::time::SyncClock::now() < open_deadline)
  {
    bridge_client->notify(epoll, bridge_client->socket_fd(), EPOLLIN);
    std::this_thread::sleep_for(std::chrono::microseconds(1));
  }
  REQUIRE(bridge_client->socket_fd() != -1);

  accepted = accept_connection(
    *listen_socket, [&bridge_client, &epoll]() { bridge_client->notify(epoll, bridge_client->timer_fd(), EPOLLIN); });
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
    CHECK(recv_acknowledgement(
      accepted,
      expected_seqnos2[i],
      [&epoll, &bridge_client]() { bridge_client->notify(epoll, bridge_client->socket_fd(), EPOLLIN); }));
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

  diagnostics_counters->max_bridge_latency = {};
  diagnostics_counters->max_latency_channel_name = {};
  CHECK(*diagnostics_counters == TcpBridgeDiagnosticsCounters{.closed_socket_count = 1});

  diagnostics_counters = std::make_shared<TcpBridgeDiagnosticsCounters>();

  bridge_client = TcpBridgeClient::make(
    memres,
    config,
    channel.make_publisher(max_observer),
    subscriber,
    jewels::memory::make_non_null_from_ref(epoll),
    diagnostics_counters);
  REQUIRE(bridge_client);

  accepted = accept_connection(
    *listen_socket, [&bridge_client, &epoll]() { bridge_client->notify(epoll, bridge_client->timer_fd(), EPOLLIN); });
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
    CHECK(recv_acknowledgement(
      accepted,
      expected_seqnos3[i],
      [&epoll, &bridge_client]() { bridge_client->notify(epoll, bridge_client->socket_fd(), EPOLLIN); }));
  }
  CHECK(testing::dump<Msg>(subscriber) == expected3);
  messages = subscriber.available();
  for (size_t i = 0; i < messages.size(); ++i)
  {
    CHECK(messages[static_cast<int64_t>(i)].header()->publish_timestamp == expected_publish_stamps3[i]);
    CHECK(messages[static_cast<int64_t>(i)].header()->sequence_number == expected_seqnos3[i]);
    CHECK(messages[static_cast<int64_t>(i)].header()->source_commit_timestamp == expected_commit_stamps3[i]);
  }
  diagnostics_counters->max_bridge_latency = {};
  diagnostics_counters->max_latency_channel_name = {};
  CHECK(*diagnostics_counters == TcpBridgeDiagnosticsCounters{});
}

} // namespace clockwork::pinion
