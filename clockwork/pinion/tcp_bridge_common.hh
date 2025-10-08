// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "clockwork/pinion/bridge_status.hh"
#include "clockwork/repr_iface.hh"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace clockwork::pinion
{

/// TCP bridge header magic nimber size
static constexpr size_t tcp_message_header_magic_number_size = 8U;

/// TCP bridge header magic number used to recover after receiving invalid message headers
static constexpr std::array<std::byte, tcp_message_header_magic_number_size> tcp_message_header_magic_number = {
  std::byte{'t'},
  std::byte{'c'},
  std::byte{'p'},
  std::byte{'b'},
  std::byte{'r'},
  std::byte{'i'},
  std::byte{'d'},
  std::byte{'g'}};

///
/// Header body for channel messages sent on a TCP socket.
///
struct TcpMessageHeaderBody
{
  /// Magic number used for framing
  std::array<std::byte, tcp_message_header_magic_number_size> magic_number{tcp_message_header_magic_number};

  /// Sequence number
  uint64_t sequence_number{};

  /// Length of the payload, excluding this header.
  uint64_t message_length{};

  /// Timestamp the message was published.
  int64_t publish_timestamp{};

  /// Timestamp the message was committed by the original publisher
  int64_t source_commit_timestamp{};
};

///
/// Header for channel messages sent on a TCP socket.
///
struct TcpMessageHeader
{
  /// Message header body
  TcpMessageHeaderBody body{};

  /// Message header body checksum
  uint64_t checksum{};
};

///
/// Tail for channel messages sent on a TCP socket.
///
struct TcpMessageTail
{
  /// Lite compressor counts checksum
  uint64_t counts_checksum{};

  /// Lite compressor data checksum
  uint64_t data_checksum{};
};

/// Struct used to store the counters for a bridge client or server
struct TcpBridgeClientServerCounters
{
  uint64_t message_count{};
  uint64_t message_bytes{};
  uint64_t compressed_message_bytes{};
  std::chrono::nanoseconds total_receive_latency{};
  std::chrono::nanoseconds total_transfer_time{};
  std::chrono::nanoseconds total_compression_time{};
  std::chrono::nanoseconds total_bridge_latency{};
  std::chrono::nanoseconds max_receive_latency{};
  std::chrono::nanoseconds max_transfer_time{};
  std::chrono::nanoseconds max_compression_time{};
  std::chrono::nanoseconds max_bridge_latency{};
};

/// Update the client or server counters
/// @param[in] message_bytes Message size in bytes
/// @param[in] compressed_message_bytes Compressed message size in bytes
/// @param[in] receive_latency Latency between publish time and when the header was received
/// @param[in] transfer_time Time to transfer the message over the socket
/// @param[in] compression_time Time to compress/decompress the message
/// @param[in[ bridge_latency Latency between the publish time and when it left the bridge
/// @param[in,out] counters Client or server counters to update
void update_client_server_counters(
  uint64_t message_bytes,
  uint64_t compressed_message_bytes,
  std::chrono::nanoseconds receive_latency,
  std::chrono::nanoseconds transfer_time,
  std::chrono::nanoseconds compression_time,
  std::chrono::nanoseconds bridge_latency,
  TcpBridgeClientServerCounters& counters);

/// Combine bridge client or server counters
/// @param[in] source Source counters to combine from
/// @param[in,out] dest Destination counters to combine into
void combine_client_server_counters(const TcpBridgeClientServerCounters& source, TcpBridgeClientServerCounters& dest);

/// Store the client or server counters in a bridge status message
/// @param[in] channel_name Channel name
/// @param[in] source Source client/server counters
/// @param[in] interval Report interval
/// @param[out] dest Destination client/server counters
void store_client_server_counters(
  std::string_view channel_name,
  const TcpBridgeClientServerCounters& source,
  std::chrono::nanoseconds interval,
  Tappy<BridgeClientServerCounters>& dest);

/// Struct used to store the counters reported in the TCP bridge diagnostics
struct TcpBridgeDiagnosticsCounters
{
  uint64_t drop_count{};
  uint64_t failed_sends{};
  uint64_t closed_socket_count{};
  uint64_t failed_recvs{};
  uint64_t failed_reservations{};
  uint64_t malformed_messages{};
  uint64_t failed_commits{};
  uint64_t failed_discards{};
  /// Then number of times we failed to accept a connection from a pending client or prepare the corresponding file
  /// descriptors.
  uint64_t client_socket_errors{};
  /// The number of times we failed to send messages to clients because of an unrecoverable progress error.
  uint64_t progress_errors{};
  /// The number of unxpected epoll errors.
  uint64_t epoll_errors{};
  /// The number of unexpected failures while publishing bridge status.
  uint64_t status_errors{};
  std::chrono::nanoseconds max_bridge_latency{};
  std::string_view max_latency_channel_name{};

  /// Comparison operator
  auto operator<=>(const TcpBridgeDiagnosticsCounters&) const = default;
};

} // namespace clockwork::pinion
