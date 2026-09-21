// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/tests/support/bridge_test_support.hh"

#include "clockwork/logging/lite_compressor.hh"
#include "clockwork/logging/onboard/types.hh"
#include "clockwork/logging/xxh3_checksum.hh"
#include "clockwork/pinion/detail/socket_common.hh"
#include "clockwork/pinion/detail/tcp_socket.hh"
#include "clockwork/pinion/tcp_bridge_common.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/networking/socket_address.hh"
#include "jewels/std/expected.hh"
#include "jewels/std/span.hh"
#include "jewels/time/sync_time.hh"

#include <arpa/inet.h>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cerrno>
#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <netinet/in.h>
#include <span>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <thread>
#include <utility>
#include <vector>

namespace clockwork::pinion::support
{

using jewels::Out;

/// Send a message in an I/O vector
/// @param[in] sock Socket
/// @param[in] header Message header
/// @param[in] data Message data
/// @param[in] trailer Message trailer
/// @return True if the send was successful
[[nodiscard]] bool send_message_in_iovec(
  int32_t sock, std::span<std::byte> header, std::span<std::byte> data = {}, std::span<std::byte> tail = {})
{
  std::array<struct iovec, 3> iovecs{};
  iovecs[0].iov_base = header.data();
  iovecs[0].iov_len = header.size();
  iovecs[1].iov_base = data.data();
  iovecs[1].iov_len = data.size();
  iovecs[2].iov_base = tail.data();
  iovecs[2].iov_len = tail.size();
  const auto payload_size = iovecs[0].iov_len + iovecs[1].iov_len + iovecs[2].iov_len;
  const struct msghdr msg{
    .msg_name = nullptr,
    .msg_namelen = 0,
    .msg_iov = iovecs.data(),
    .msg_iovlen = iovecs.size(),
    .msg_control = nullptr,
    .msg_controllen = 0,
    .msg_flags = 0,
  };
  const auto send_rc = ::sendmsg(sock, &msg, MSG_NOSIGNAL);
  CHECK(send_rc == static_cast<ssize_t>(payload_size));
  return send_rc == static_cast<ssize_t>(payload_size);
}

[[nodiscard]] bool
compare_diagnostics_counters(TcpBridgeDiagnosticsCounters counters, TcpBridgeDiagnosticsCounters expected_counters)
{
  // Clear the max latencies and max latency channel names, we don't expect these to match
  counters.max_bridge_latency = {};
  counters.max_bridge_bulk_data_latency = {};
  counters.max_latency_channel_name = {};
  counters.max_bulk_data_latency_channel_name = {};
  expected_counters.max_bridge_latency = {};
  expected_counters.max_latency_channel_name = {};
  CHECK(counters.drop_count == expected_counters.drop_count);
  // Failed sends should be at least as big as the expected value
  CHECK(counters.failed_sends >= expected_counters.failed_sends);
  // Set the count back to the expected value so the counters match the expected counters
  counters.failed_sends = expected_counters.failed_sends;
  // The closed socket count should be at least as big as the expected value
  CHECK(counters.closed_socket_count >= expected_counters.closed_socket_count);
  // Set the count back to the expected value so the counters match the expected counters
  counters.closed_socket_count = expected_counters.closed_socket_count;
  CHECK(counters.failed_recvs >= expected_counters.failed_recvs);
  // The failed receive count should be at least as big as the expected value
  counters.failed_recvs = expected_counters.failed_recvs;
  // Set the count back to the expected value so the counters match the expected counters
  CHECK(counters.failed_reservations == expected_counters.failed_reservations);
  CHECK(counters.malformed_messages == expected_counters.malformed_messages);
  CHECK(counters.failed_commits == expected_counters.failed_commits);
  CHECK(counters.failed_discards == expected_counters.failed_discards);
  CHECK(counters.client_socket_errors == expected_counters.client_socket_errors);
  CHECK(counters.progress_errors == expected_counters.progress_errors);
  CHECK(counters.epoll_errors == expected_counters.epoll_errors);
  CHECK(counters.status_errors == expected_counters.status_errors);
  return counters == expected_counters;
}

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
      .payload_type = PayloadType::message,
      .sequence_number = seqno,
      .message_length = message_len,
      .publish_timestamp = publish_timestamp,
      .source_commit_timestamp = commit_timestamp,
    }};
  header.checksum = clockwork_logging::compute_xxh3_checksum(std::as_bytes(jewels::as_single_item_span(header.body)));
  REQUIRE(send_message_in_iovec(sock, std::as_writable_bytes(jewels::as_single_item_span(header))));
}

void send_payload(
  int sock, uint64_t seqno, int64_t publish_timestamp, int64_t commit_timestamp, std::span<const std::byte> message)
{
  TcpMessageTail tail{};
  std::vector<std::byte> compressed_message;
  compress_message(Out{compressed_message}, Out{tail.counts_checksum}, Out{tail.data_checksum}, message);
  TcpMessageHeader header{
    .body = TcpMessageHeaderBody{
      .payload_type = PayloadType::message,
      .sequence_number = seqno,
      .message_length = compressed_message.size(),
      .publish_timestamp = publish_timestamp,
      .source_commit_timestamp = commit_timestamp}};
  header.checksum = clockwork_logging::compute_xxh3_checksum(std::as_bytes(jewels::as_single_item_span(header.body)));
  REQUIRE(send_message_in_iovec(
    sock,
    std::as_writable_bytes(jewels::as_single_item_span(header)),
    compressed_message,
    std::as_writable_bytes(jewels::as_single_item_span(tail))));
}

void send_corrupted_payload(
  int sock,
  uint64_t seqno,
  int64_t publish_timestamp,
  int64_t commit_timestamp,
  std::span<const std::byte> message,
  CorruptionType corruption_type)
{
  TcpMessageTail tail{};
  std::vector<std::byte> compressed_message;
  compress_message(Out{compressed_message}, Out{tail.counts_checksum}, Out{tail.data_checksum}, message);
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
      .payload_type = PayloadType::message,
      .sequence_number = seqno,
      .message_length = compressed_message.size(),
      .publish_timestamp = publish_timestamp,
      .source_commit_timestamp = commit_timestamp}};
  if (corruption_type == CorruptionType::corrupt_size)
  {
    // Increment the message length in the header so the size is corrupt
    header.body.message_length =
      message.size() + clockwork_logging::LiteCompressor::max_compression_overhead_bytes + 1U;
  }
  else if (corruption_type == CorruptionType::corrupt_payload_type)
  {
    header.body.payload_type = PayloadType::invalid;
  }
  else if (corruption_type == CorruptionType::corrupt_magic_number)
  {
    header.body.magic_number.at(0U) = std::byte{};
  }
  header.checksum = clockwork_logging::compute_xxh3_checksum(std::as_bytes(jewels::as_single_item_span(header.body)));
  if (corruption_type == CorruptionType::corrupt_header_checksum)
  {
    ++header.checksum;
  }
  REQUIRE(send_message_in_iovec(
    sock,
    std::as_writable_bytes(jewels::as_single_item_span(header)),
    compressed_message,
    std::as_writable_bytes(jewels::as_single_item_span(tail))));
}

void send_extended_payload(
  int sock,
  uint64_t seqno,
  uint64_t message_len,
  int64_t publish_timestamp,
  int64_t commit_timestamp,
  TcpMessageTail& tail)
{
  std::vector<std::byte> payload(message_len + clockwork_logging::LiteCompressor::max_compression_overhead_bytes + 1U);
  TcpMessageHeader header{
    .body = TcpMessageHeaderBody{
      .payload_type = PayloadType::message,
      .sequence_number = seqno,
      .message_length = payload.size(),
      .publish_timestamp = publish_timestamp,
      .source_commit_timestamp = commit_timestamp}};
  header.checksum = clockwork_logging::compute_xxh3_checksum(std::as_bytes(jewels::as_single_item_span(header.body)));
  REQUIRE(send_message_in_iovec(
    sock,
    std::as_writable_bytes(jewels::as_single_item_span(header)),
    payload,
    std::as_writable_bytes(jewels::as_single_item_span(tail))));
}

[[nodiscard]] bool socket_is_closed(int32_t sock, std::chrono::nanoseconds timeout)
{
  const auto deadline = jewels::time::SyncClock::now() + timeout;
  uint8_t datum{};
  while (true)
  {
    const auto bytes_recvd = ::recv(sock, &datum, 1U, 0);
    if (bytes_recvd == -1 && errno == EAGAIN && jewels::time::SyncClock::now() < deadline)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }
    if (bytes_recvd == -1)
    {
      CHECK(errno == ECONNRESET);
      return errno == ECONNRESET;
    }
    CHECK(bytes_recvd == 0);
    return bytes_recvd == 0;
  }
}

[[nodiscard]] int32_t accept_connection(const TcpSocket& listen_socket)
{
  const auto accept_deadline = jewels::time::SyncClock::now() + default_recv_timeout;
  while (true)
  {
    const auto accepted = ::accept(listen_socket.descriptor(), nullptr, nullptr);
    if (accepted < 0 && errno == EAGAIN && jewels::time::SyncClock::now() < accept_deadline)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }
    return accepted;
  }
}

[[nodiscard]] bool recv_acknowledgement(int32_t sock, uint64_t seqno)
{
  const auto recv_deadline = jewels::time::SyncClock::now() + default_recv_timeout;
  uint8_t ack_byte{};
  while (true)
  {
    const auto bytes_recvd = ::recv(sock, &ack_byte, 1U, 0);
    if (bytes_recvd == -1 && errno == EAGAIN && jewels::time::SyncClock::now() < recv_deadline)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
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

[[nodiscard]] std::pair<std::unique_ptr<TcpSocket>, jewels::networking::SocketAddress> make_bound_socket()
{
  jewels::filesystem::FileDescriptor sock{::socket(AF_INET, SOCK_STREAM, 0)};
  REQUIRE(sock);
  const int reuse_addr_opt = 1;
  REQUIRE(::setsockopt(*sock, SOL_SOCKET, SO_REUSEADDR, &reuse_addr_opt, sizeof(reuse_addr_opt)) == 0);
  const auto ephemeral_addr = jewels::networking::SocketAddress::create(std::string{local_socket_host}, 0);
  REQUIRE(ephemeral_addr);
  REQUIRE(::bind(*sock, ephemeral_addr->ptr(), jewels::networking::SocketAddress::byte_size()) == 0);
  ::sockaddr_in actual_addr{};
  socklen_t addr_size = sizeof(actual_addr);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) For socket API
  REQUIRE(::getsockname(*sock, reinterpret_cast<::sockaddr*>(&actual_addr), &addr_size) == 0);
  const auto bound_addr =
    jewels::networking::SocketAddress::create(std::string{local_socket_host}, ::ntohs(actual_addr.sin_port));
  REQUIRE(bound_addr);
  return {std::make_unique<TcpSocket>(std::move(sock)), *bound_addr};
}

[[nodiscard]] std::pair<std::unique_ptr<TcpSocket>, jewels::networking::SocketAddress> make_listen_socket()
{
  constexpr auto backlog = 1;
  const auto ephemeral_addr = jewels::networking::SocketAddress::create(std::string{local_socket_host}, 0);
  auto listen_socket = TcpSocket::create_listen(*ephemeral_addr, backlog);
  REQUIRE(listen_socket);
  ::sockaddr_in actual_addr{};
  socklen_t addr_size = sizeof(actual_addr);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) For socket API
  REQUIRE(::getsockname(listen_socket->descriptor(), reinterpret_cast<::sockaddr*>(&actual_addr), &addr_size) == 0);
  const auto listen_addr =
    jewels::networking::SocketAddress::create(std::string{local_socket_host}, ::ntohs(actual_addr.sin_port));
  REQUIRE(listen_addr);
  REQUIRE(set_nonblocking(listen_socket->descriptor(), true));
  return {std::make_unique<TcpSocket>(*std::move(listen_socket)), *listen_addr};
}

[[nodiscard]] std::unique_ptr<TcpSocket> make_listen_socket(const jewels::networking::SocketAddress& listen_addr)
{
  constexpr auto backlog = 1;
  auto listen_socket = TcpSocket::create_listen(listen_addr, backlog);
  REQUIRE(listen_socket);
  REQUIRE(set_nonblocking(listen_socket->descriptor(), true));
  return std::make_unique<TcpSocket>(*std::move(listen_socket));
}

[[nodiscard]] bool receive_buffer(int sock, std::span<std::byte> recv_buffer, std::chrono::nanoseconds recv_timeout)
{
  const auto recv_deadline = jewels::time::SyncClock::now() + recv_timeout;
  while (!recv_buffer.empty())
  {
    auto recv_bytes = ::recv(sock, recv_buffer.data(), recv_buffer.size(), 0);
    if (recv_bytes < 0)
    {
      if (errno == EAGAIN && jewels::time::SyncClock::now() < recv_deadline)
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
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

[[nodiscard]] std::optional<TcpMessageHeader> recv_header(int sock, std::chrono::nanoseconds recv_timeout)
{
  TcpMessageHeader header{};
  if (!receive_buffer(sock, std::as_writable_bytes(jewels::as_single_item_span(header)), recv_timeout))
  {
    return std::nullopt;
  }
  return header;
}

[[nodiscard]] bool recv_null_header(
  int sock,
  uint64_t expected_sequence_number,
  const std::unordered_set<PayloadType>& expected_payload_types,
  std::chrono::nanoseconds recv_timeout)
{
  const auto recv_deadline = jewels::time::SyncClock::now() + recv_timeout;
  while (true)
  {
    const auto maybe_header = recv_header(sock, recv_timeout);
    if (!maybe_header)
    {
      if (jewels::time::SyncClock::now() > recv_deadline)
      {
        return false;
      }
      continue;
    }
    if (
      maybe_header->checksum !=
      clockwork_logging::compute_xxh3_checksum(std::as_bytes(jewels::as_single_item_span(maybe_header->body))))
    {
      CHECK(
        maybe_header->checksum ==
        clockwork_logging::compute_xxh3_checksum(std::as_bytes(jewels::as_single_item_span(maybe_header->body))));
      return false;
    }
    if (!expected_payload_types.contains(maybe_header->body.payload_type))
    {
      continue;
    }
    if (maybe_header->body.sequence_number < expected_sequence_number)
    {
      continue;
    }
    CHECK(maybe_header->body.sequence_number == expected_sequence_number);
    return maybe_header->body.sequence_number == expected_sequence_number;
  }
}

[[nodiscard]] bool send_acknowledgement(int sock, uint64_t sequence_number)
{
  auto ack_byte = static_cast<uint8_t>(sequence_number);
  auto send_bytes = ::send(sock, &ack_byte, 1U, MSG_NOSIGNAL);
  return send_bytes == 1;
}

[[nodiscard]] bool
check_num_clients(const TcpBridgeServer& server, size_t expected_num, std::chrono::nanoseconds timeout)
{
  const auto deadline = jewels::time::SyncClock::now() + timeout;
  while (server.get_num_clients() != expected_num)
  {
    if (jewels::time::SyncClock::now() > deadline)
    {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return true;
}

} // namespace clockwork::pinion::support
