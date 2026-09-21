// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "clockwork/pinion/bridge_status_clk_cc.hh"
#include "clockwork/repr_iface.hh"

#include <wise_enum.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string_view>

namespace clockwork::pinion
{

/// Interval for sending null headers while waiting for an acknowledgement
static constexpr auto tcp_bridge_null_header_interval = std::chrono::milliseconds(20);

/// Minimum interval between sends to keep the TCP connection alive
static constexpr auto tcp_bridge_keep_alive_interval = std::chrono::milliseconds(1500);

/// Minimum reportable bridge latency
static constexpr auto min_reportable_bridge_latency = std::chrono::milliseconds(25);

/// Minimum reportable bridge bulk data latency
static constexpr auto min_reportable_bridge_bulk_data_latency = std::chrono::milliseconds(1950);

/// Maximum bridge bulk data transmit delay
constexpr auto max_bridge_bulk_data_transmit_delay = std::chrono::milliseconds(1900);

/// Time to wait before closing and reopening a stuck TCP connection
static constexpr auto tcp_bridge_reconnect_interval = std::chrono::seconds(1);

/// TCP bridge header magic number size
static constexpr size_t tcp_message_header_magic_number_size = 7U;

/// TCP bridge header magic number used to validate headers
static constexpr std::array<std::byte, tcp_message_header_magic_number_size> tcp_message_header_magic_number = {
  std::byte{'t'}, std::byte{'c'}, std::byte{'p'}, std::byte{'b'}, std::byte{'r'}, std::byte{'i'}, std::byte{'d'}};

/// Payload type for messages sent on a TCP socket.
WISE_ENUM_CLASS(
  (PayloadType, int8_t),
  (invalid, 0),      // Invalid
  (keep_alive, 107), // Keep alive ('k')
  (message, 109),    // Message ('m')
  (null_header, 110) // Null header ('n')
)

///
/// Header body for channel messages sent on a TCP socket.
///
struct __attribute__((packed)) TcpMessageHeaderBody
{
  /// Magic number used for framing
  std::array<std::byte, tcp_message_header_magic_number_size> magic_number{tcp_message_header_magic_number};

  /// Payload type
  PayloadType payload_type{};

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
struct __attribute__((packed)) TcpMessageHeader
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
  uint64_t null_header_count{};
  uint64_t keep_alive_count{};
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
  std::chrono::nanoseconds max_bridge_bulk_data_latency{};
  std::string_view max_bulk_data_latency_channel_name{};

  /// Comparison operator
  auto operator<=>(const TcpBridgeDiagnosticsCounters&) const = default;
};

/// Combine counters from a diagnostics report into the counters for a bridge status report
/// @param[in] source Source counters to combine from
/// @param[in,out] dest Destination counters to combine into
void combine_diagnostics_counters(const TcpBridgeDiagnosticsCounters& source, TcpBridgeDiagnosticsCounters& dest);

/// Thread safe container for the counters reported in the TCP bridge diagnostics
class TcpBridgeDiagnosticsState
{
public:
  TcpBridgeDiagnosticsState() noexcept = default;
  ~TcpBridgeDiagnosticsState() noexcept = default;

  TcpBridgeDiagnosticsState(const TcpBridgeDiagnosticsState&) = delete;
  TcpBridgeDiagnosticsState& operator=(const TcpBridgeDiagnosticsState&) = delete;
  TcpBridgeDiagnosticsState(TcpBridgeDiagnosticsState&&) = delete;
  TcpBridgeDiagnosticsState& operator=(TcpBridgeDiagnosticsState&&) = delete;

  /// Get and reset the diagnostics counters
  TcpBridgeDiagnosticsCounters get_and_reset_counters();

  /// Increment the drop count
  void increment_drop_count(size_t count = 1U);

  /// Increment the failed send count
  void increment_failed_sends(size_t count = 1U);

  /// Increment the closed socket count
  void increment_closed_socket_count(size_t count = 1U);

  /// Incremement the failed receive count
  void increment_failed_recvs(size_t count = 1U);

  /// Increment the failed reservation count
  void increment_failed_reservations(size_t count = 1U);

  /// Increment the malformed message count
  void increment_malformed_messages(size_t count = 1U);

  /// Increment the failed commit count
  void increment_failed_commits(size_t count = 1U);

  /// Increment the failed discard count
  void increment_failed_discards(size_t count = 1U);

  /// Increment the client socket error count
  void increment_client_socket_errors(size_t count = 1U);

  /// Increment the progress error count
  void increment_progress_errors(size_t count = 1U);

  /// Increment the epoll count
  void increment_epoll_errors(size_t count = 1U);

  /// Increment the status error count
  void increment_status_errors(size_t count = 1U);

  /// Update the max bridge latency
  /// @param[in] latency Bridge latency
  /// @param[in] channel_name Channel name
  void update_max_bridge_latency(std::chrono::nanoseconds latency, std::string_view channel_name);

  /// Update the max bridge bulk data latency
  /// @param[in] latency Bridge latency
  /// @param[in] channel_name Channel name
  void update_max_bridge_bulk_data_latency(std::chrono::nanoseconds latency, std::string_view channel_name);

private:
  /// Mutex used to serialize access to the counters
  std::mutex mutex_;

  /// Diagnostics counters
  TcpBridgeDiagnosticsCounters counters_;
};

} // namespace clockwork::pinion
