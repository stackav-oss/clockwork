// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "clockwork/pinion/bridge_status.hh"
#include "clockwork/repr_iface.hh"

#include <chrono>
#include <cstdint>
#include <span>
#include <string_view>
#include <sys/socket.h>

namespace clockwork::pinion
{

///
/// Header for channel messages sent on a TCP socket.
///
struct TcpMessageHeader
{
  uint64_t sequence_number{};

  /// Length of the payload, excluding this header.
  uint64_t message_length{};

  /// Timestamp the message was published.
  int64_t publish_timestamp{};

  /// Timestamp the message was committed by the original publisher
  int64_t source_commit_timestamp{};
};

///
/// Tail for channel messages sent on a TCP socket.
///
struct TcpMessageTail
{
  /// True if the receiver can safely commit the message.
  bool commit{};
};

/// Subtract bytes from a sequence of iovecs.
/// @param iovecs the iovecs to modify
/// @param amount the number of bytes to advance iovecs by
/// @note This assumes that the amount being subtracted is less than or equal to
/// the total length of the provided iovecs.
void advance_iovecs(std::span<struct iovec> iovecs, size_t amount);

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
};

} // namespace clockwork::pinion
