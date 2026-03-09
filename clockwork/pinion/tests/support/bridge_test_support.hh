// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/pinion/detail/tcp_socket.hh"
#include "clockwork/pinion/tcp_bridge_common.hh"
#include "clockwork/pinion/tcp_bridge_server.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/networking/socket_address.hh"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace clockwork::pinion::support
{

/// Timeout for receiving from the client
static constexpr auto default_recv_timeout = std::chrono::seconds(10);

/// Timeout for receiving from the server when we don't expect anything
static constexpr auto short_recv_timeout = std::chrono::milliseconds(1);

/// Host name for a local socket
static constexpr auto local_socket_host = std::string_view{"127.0.0.1"};

/// Compare counters against the expected counters.
///
/// Latency values are ignored.
///
/// @param[in] counters Counters to compare
/// @param[in] expected_counters Expected counter values
/// @return True if the counter values match
[[nodiscard]] bool
compare_diagnostics_counters(TcpBridgeDiagnosticsCounters counters, TcpBridgeDiagnosticsCounters expected_counters);

/// Compress a message
/// @param[out] compressed_message Compressed message
/// @param[out] counts_checksum Message counts checksum
/// @param[out] data_checksum Message data checksum
/// @param[in] message Message to compress
void compress_message(
  jewels::Out<std::vector<std::byte>> compressed_message,
  jewels::Out<uint64_t> counts_checksum,
  jewels::Out<uint64_t> data_checksum,
  std::span<const std::byte> message);

/// Send a message header over a socket
/// @param[in] sock Socket
/// @param[in] seqno Sequence number
/// @param[in] message_len Message length
/// @param[in] publish_timestamp Publish timestamp
/// @param[in] commit_timestamp Commit timestamp
void send_header(int sock, uint64_t seqno, uint64_t message_len, int64_t publish_timestamp, int64_t commit_timestamp);

/// Send a message header and payload over a socket
/// @param[in] sock Socket
/// @param[in] seqno Sequence number
/// @param[in] message_len Message length
/// @param[in] publish_timestamp Publish timestamp
/// @param[in] commit_timestamp Commit timestamp
/// @param[in] message Message payload
void send_payload(
  int sock, uint64_t seqno, int64_t publish_timestamp, int64_t commit_timestamp, std::span<const std::byte> message);

/// Type of corruption to send in corrupted payload
enum class CorruptionType : uint8_t
{
  corrupt_size,
  corrupt_magic_number,
  corrupt_header_checksum,
  corrupt_counts,
  corrupt_data,
  corrupt_payload_type
};

/// Send a corrupted message
/// @param[in] sock Socket
/// @param[in] seqno Sequence number
/// @param[in] publish_timestamp Publish timestamp
/// @param[in] commit_timestamp Commit timestamp
/// @param[in] message Message payload
/// @param[in] corruption_type Corruption type
void send_corrupted_payload(
  int sock,
  uint64_t seqno,
  int64_t publish_timestamp,
  int64_t commit_timestamp,
  std::span<const std::byte> message,
  CorruptionType corruption_type);

/// Send a message with extra bytes in the payload
/// @param[in] sock Socket
/// @param[in] seqno Sequence number
/// @param[in] message_len Message length
/// @param[in] publish_timestamp Publish timestamp
/// @param[in] commit_timestamp Commit timestamp
/// @param[in] tail Message tail
void send_extended_payload(
  int sock,
  uint64_t seqno,
  uint64_t message_len,
  int64_t publish_timestamp,
  int64_t commit_timestamp,
  TcpMessageTail& tail);

/// Test whether a socket is closed
/// @param[in] sock Socket
/// @param[in] timeout Time to wait for the socket to close
/// @return True if the socket is closed
[[nodiscard]] bool socket_is_closed(int32_t sock, std::chrono::nanoseconds timeout = default_recv_timeout);

/// Accept a connection from a socket
/// @param[in] listen_socket Listening TCP socket
/// @return Accepted socket
[[nodiscard]] int32_t accept_connection(const TcpSocket& listen_socket);

/// Receive an acknowledgement on a socket
/// @param[in] sock Socket
/// @param[in] seqno Expected sequence number
/// @return True if an acknowledgement was received
[[nodiscard]] bool recv_acknowledgement(int32_t sock, uint64_t seqno);

/// Make a socket bound to an address and get the bound address
/// @return Socket and bound address
[[nodiscard]] std::pair<std::unique_ptr<TcpSocket>, jewels::networking::SocketAddress> make_bound_socket();

/// Make a listening socket and get the bound address
/// @return Socket and bound address
[[nodiscard]] std::pair<std::unique_ptr<TcpSocket>, jewels::networking::SocketAddress> make_listen_socket();

/// Make a listening socket
/// @param[in] listen_addr Listening address
/// @return Socket
[[nodiscard]] std::unique_ptr<TcpSocket> make_listen_socket(const jewels::networking::SocketAddress& listen_addr);

/// Receive a buffer from the socket
/// @param[in] sock Socket
/// @param[in] recv_buffer Receive buffer
/// @param[in] recv_timeout Receive timeout
/// @return True on success
[[nodiscard]] bool receive_buffer(
  int sock, std::span<std::byte> recv_buffer, std::chrono::nanoseconds recv_timeout = default_recv_timeout);

/// Receive a message header
/// @param[in] sock Socket
/// @param[in] recv_timeout Receive timeout
/// @return Message header on success
[[nodiscard]] std::optional<TcpMessageHeader>
recv_header(int sock, std::chrono::nanoseconds recv_timeout = default_recv_timeout);

/// Receive a null header
/// @param[in] sock Socket
/// @param[in] expected_seqno Expected sequence number
/// @param[in] payload_type Expected payload type
/// @param[in] recv_timeout Receive timeout
/// @return True on success
[[nodiscard]] bool recv_null_header(
  int sock,
  uint64_t expected_sequence_number,
  const std::unordered_set<PayloadType>& expected_payload_types = {PayloadType::null_header, PayloadType::keep_alive},
  std::chrono::nanoseconds recv_timeout = default_recv_timeout);

/// Send an acknowledgement
/// @param[in] sock Socket
/// @param[in] sequence_number Sequence number
/// @return True on success
[[nodiscard]] bool send_acknowledgement(int sock, uint64_t sequence_number);

/// Check that the number of clients matches the expected number
/// @param[in] server TCP bridge server
/// @param[in] expected_num Expected number of clients
/// @param[in] timeout Amount of time to wait for the number of clients to match
[[nodiscard]] bool check_num_clients(
  const TcpBridgeServer& server, size_t expected_num, std::chrono::nanoseconds timeout = default_recv_timeout);

/// Decompress a message
/// @tparam Msg Message type
/// @param[in] counts_checksum Expected counts checksum
/// @param[in] data_checksum Expected data checksum
/// @param[in] compressed_data Compressed message data
/// @return Decompressed message or nullptr on error
template <typename Msg>
[[nodiscard]] std::unique_ptr<Msg>
decompress_message(uint64_t counts_checksum, uint64_t data_checksum, std::span<const std::byte> compressed_data);

/// Receive and message and unpack the header, payload and tail
/// @tparam Msg Message type
/// @param[in] sock Socket
/// @param[in] expected_message Expected message
/// @param[in] recv_timeout Receive timeout
/// @return Header, data and tail on success
template <typename Msg>
[[nodiscard]] std::optional<std::tuple<TcpMessageHeader, std::unique_ptr<Msg>, TcpMessageTail>>
recv_and_unpack(int sock, const Msg& expected_message, std::chrono::nanoseconds recv_timeout = default_recv_timeout);

/// Receive a message and check the payload
/// @tparam Msg Message type
/// @param[in] sock Socket
/// @param[in] expected_seqno Expected sequence number
/// @param[in] expected_publish_time Expected publish timestamp
/// @param[in] expected_commit_time Expected commit timestamp
/// @param[in] recv_timeout Receive timeout
/// @return True on success
template <typename Msg>
[[nodiscard]] bool check_next_payload(
  int sock,
  uint64_t expected_seqno,
  const Msg& expected_message,
  int64_t expected_publish_time,
  int64_t expected_commit_time,
  std::chrono::nanoseconds recv_timeout = default_recv_timeout);

/// Receive a message and check the payload
/// @tparam Msg Message type
/// @param[in] sock Socket
/// @param[in] expected_seqno Expected sequence number
/// @param[in] recv_timeout Receive timeout
/// @return True on success
template <typename Msg>
[[nodiscard]] bool check_next_payload(
  int sock,
  uint64_t expected_seqno,
  const Msg& expected_message,
  std::chrono::nanoseconds recv_timeout = default_recv_timeout);

/// Make a random message payload
/// @tparam message_size Message size
/// @return Random message payload
template <size_t message_size>
[[nodiscard]] std::unique_ptr<std::array<std::byte, message_size>> make_random_message();

} // namespace clockwork::pinion::support

#include "clockwork/pinion/tests/support/bridge_test_support.inl"
