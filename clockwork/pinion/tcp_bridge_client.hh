// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/lite_compressor.hh"
#include "clockwork/pinion/detail/tcp_socket.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "clockwork/pinion/tcp_bridge_common.hh"
#include "clockwork/pinion/tcp_bridge_config_clk_cc.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/networking/socket_address.hh"
#include "jewels/time/sync_time.hh"

#include <wise_enum.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace clockwork::pinion
{

///
/// A TCP client that receives messages from a TcpBridgeServer and writes them
/// into an ShmChannel.
///
struct TcpBridgeClient
{
public:
  /// Validate header result
  WISE_ENUM_CLASS_MEMBER((ValidateHeaderResult, uint8_t), null_header, valid, bad_checksum, invalid)

  /// Bridge client state
  WISE_ENUM_CLASS_MEMBER((State, uint8_t), disconnected, connecting, idle, receiving_header, receiving_message)

  /// TCP Bridge client constructor parameters
  struct TcpBridgeClientParams
  {
    /// Memory resource
    jewels::memory::MemoryResource memres;
    /// Channel name
    std::string_view channel_name;
    /// True if the channel holds bulk data
    bool is_bulk_data;
    /// Publisher handle
    PublisherHandle publisher;
    /// Subscriber handle
    SubscriberHandle subscriber;
    /// Server address
    jewels::networking::SocketAddress server_address;
    /// Minimum sequence number
    uint64_t min_sequence_number;
    /// Diagnostics counter state
    std::shared_ptr<TcpBridgeDiagnosticsState> diagnostics_state;
  };

  /// Constructor
  /// @param[in] params Constructor parameters
  explicit TcpBridgeClient(TcpBridgeClientParams& params);

  ~TcpBridgeClient();

  TcpBridgeClient() = delete;
  TcpBridgeClient(const TcpBridgeClient&) = delete;
  TcpBridgeClient& operator=(const TcpBridgeClient&) = delete;
  TcpBridgeClient(TcpBridgeClient&&) = delete;
  TcpBridgeClient& operator=(TcpBridgeClient&&) = delete;

  /// Create a TCP bridge client
  /// @param[in] memres Memory resource
  /// @param[in] config TCP bridge client config
  /// @param[in] publisher Pinion publisher handle
  /// @param[in] subscriber Pinion subscriber handle
  /// @param[in] diagnostics_state Bridge diagnostics counter state
  /// @return TCP bridge client pointer or nullptr on error
  [[nodiscard]] static std::shared_ptr<TcpBridgeClient> make(
    const jewels::memory::MemoryResource& memres,
    const Tappy<TcpBridgeClientConfig>& config,
    PublisherHandle publisher,
    SubscriberHandle subscriber,
    std::shared_ptr<TcpBridgeDiagnosticsState> diagnostics_state);

  /// Tell the worker thread to stop running
  void request_stop();

  /// Gets the TCP socket descriptor.
  [[nodiscard]] int socket_fd() const;

  /// @return Channel name
  [[nodiscard]] std::string_view channel_name() const;

  /// Get and reset the bridge client counters
  /// @return Bridge client counters
  [[nodiscard]] TcpBridgeClientServerCounters get_and_reset_counters();

private:
  /// Process the connection in a worker thread
  void worker_thread_main();

  /// Send an acknowledgment so the server knows we received the last message
  void send_acknowledgement();

  /// Starts connecting to the server
  void start_connect();

  /// Completes the connection to the server
  void complete_connect();

  /// Close the current socket
  void close_socket();

  /// Process data read from the socket into current payload
  void receive_from_socket();

  /// Validate the header received from the server
  /// @return Validate result
  [[nodiscard]] ValidateHeaderResult validate_header();

  /// Receive the message header
  void receive_header();

  /// Receive the message data and tail
  void receive_message();

  /// Channel name
  std::pmr::string channel_name_;

  /// True if the channel holds bulk data
  bool is_bulk_data_;

  /// Pinion publisher handle
  PublisherHandle publisher_;

  /// Pinion subscriber handle
  SubscriberHandle subscriber_;

  /// Socket used for connection to the server
  std::optional<TcpSocket> socket_;

  /// Server address
  jewels::networking::SocketAddress server_address_;

  /// Client state
  State state_{State::disconnected};

  /// Stop requested flag
  std::atomic<bool> stop_requested_{false};

  /// Initial connect flag
  bool initial_connect_{true};

  /// Mutex to serialize access to the counters
  std::mutex mutex_;

  /// Worker thread
  std::thread worker_thread_;

  /// Storage for the TCP message header
  TcpMessageHeader header_{};

  // If a prior call to receive_message() only partially part of a full
  // payload, this member will hold the data for the remaining data to be read.
  // In the common case, this should be zeroed empty.
  //
  // The buffer backing this span depends on the state.
  //    - If the state is receiving_header then payload_ covers the unread portion of the
  //      next message header.
  //    - If the state is receiving_message then payload_ covers the unread portion of the
  //      next message data and tail.
  std::span<std::byte> payload_;

  /// Bridge diagnostics state
  std::shared_ptr<TcpBridgeDiagnosticsState> diagnostics_state_;

  /// Compressor used to decompress messages
  clockwork_logging::LiteCompressor lite_compressor_;

  /// Message receive buffer
  std::pmr::vector<std::byte> recv_buffer_;

  /// Current message receive timestamp
  jewels::time::SyncTime current_receive_time_;

  /// Bridge client counters
  TcpBridgeClientServerCounters client_counters_;

  /// Last sequence number received
  uint64_t last_sequence_number_{};

  /// Minimum sequence number to accept
  uint64_t min_sequence_number_{};

  /// Last time data was received
  jewels::time::SyncTime last_receive_time_;
};

} // namespace clockwork::pinion
