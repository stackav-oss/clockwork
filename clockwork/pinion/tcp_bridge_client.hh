// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/abstract_epoll_manager.hh"
#include "clockwork/logging/lite_compressor.hh"
#include "clockwork/pinion/detail/tcp_socket.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "clockwork/pinion/tcp_bridge_common.hh"
#include "clockwork/pinion/tcp_bridge_config.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/networking/socket_address.hh"
#include "jewels/time/sync_time.hh"

#include <wise_enum.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace clockwork::pinion
{

///
/// A TCP client that receives messages from a TcpBridgeServer and writes them
/// into an ShmChannel.
///
struct TcpBridgeClient : public AbstractEPollCallback
{
public:
  /// Return values receiving from the socket
  WISE_ENUM_CLASS_MEMBER(
    (ReceiveResult, uint8_t),
    /// The payload for the current state is complete and ready to be processed
    payload_complete,
    /// The last call to read from the socket did not block, the client should keep reading
    keep_reading,
    /// All input on the socket has been consumed, the client should wait for the next call to notify
    input_consumed)

  /// Validate header result
  WISE_ENUM_CLASS_MEMBER((ValidateHeaderResult, uint8_t), null_header, valid, bad_checksum, invalid)

  /// Bridge client state
  WISE_ENUM_CLASS_MEMBER(
    (State, uint8_t), disconnected, connecting, idle, receiving_header, receiving_message, searching_for_next_header)

  explicit TcpBridgeClient(
    const jewels::memory::MemoryResource& memres,
    jewels::memory::ObjectPtr<AbstractEPollManager> epoll,
    std::string_view channel_name,
    PublisherHandle&& publisher,
    SubscriberHandle subscriber,
    jewels::filesystem::FileDescriptor&& timer_fd,
    jewels::networking::SocketAddress server_address,
    uint64_t min_sequence_number,
    std::shared_ptr<TcpBridgeDiagnosticsCounters> diagnostics_counters);

  ~TcpBridgeClient() noexcept override = default;

  TcpBridgeClient() = delete;
  TcpBridgeClient(const TcpBridgeClient&) = delete;
  TcpBridgeClient& operator=(const TcpBridgeClient&) = delete;
  TcpBridgeClient(TcpBridgeClient&&) = default;
  TcpBridgeClient& operator=(TcpBridgeClient&&) = delete;

  /// Create a TCP bridge client
  /// @param[in] memres Memory resource
  /// @param[in] config TCP bridge client config
  /// @param[in] publisher Pinion publisher handle
  /// @param[in] subscriber Pinion subscriber handle
  /// @param[in] epoll EPoll manager
  /// @param[in] diagnostics_counters Bridge diagnostics counters
  /// @return TCP bridge client pointer or nullptr on error
  [[nodiscard]] static std::shared_ptr<TcpBridgeClient> make(
    const jewels::memory::MemoryResource& memres,
    const TcpBridgeClientConfigTap& config,
    PublisherHandle&& publisher,
    SubscriberHandle subscriber,
    jewels::memory::ObjectPtr<AbstractEPollManager> epoll,
    const std::shared_ptr<TcpBridgeDiagnosticsCounters>& diagnostics_counters);

  /// Gets the TCP socket descriptor.
  [[nodiscard]] int socket_fd() const;

  /// Gets the timer file descriptor
  [[nodiscard]] int timer_fd() const;

  /// Handle an epoll event.
  void notify(AbstractEPollManager& epoll, int efd, uint32_t events) override;

  /// @return Channel name
  [[nodiscard]] std::string_view channel_name() const;

  /// Get and reset the bridge client counters
  /// @return Bridge client counters
  [[nodiscard]] TcpBridgeClientServerCounters get_and_reset_counters();

private:
  /// Send an acknowledgment so the server knows we received the last message
  /// @return Receive result
  [[nodiscard]] ReceiveResult send_acknowledgement();

  /// Set the state to disconnected and start a timer to reconnect to the server
  void set_reconnect_timer();

  /// Starts an async connect to the server
  /// @return Receive result
  [[nodiscard]] ReceiveResult start_reconnect();

  /// Completes the async connect to the server
  /// @return Receive result
  [[nodiscard]] ReceiveResult complete_reconnect();

  /// Search for the next message header
  /// @return Receive result
  [[nodiscard]] ReceiveResult search_for_next_header();

  /// Close the current socket and start the reconnect timer
  /// @return Receive result
  [[nodiscard]] ReceiveResult close_socket_and_start_reconnect_timer();

  /// Process data read from the socket into current payload
  /// @return True if more data may be available, false if all data has been consumed
  [[nodiscard]] ReceiveResult receive_from_socket();

  /// Validate the header received from the server
  /// @return Validate result
  [[nodiscard]] ValidateHeaderResult validate_header();

  /// Receive the message header
  /// @return Receive result
  [[nodiscard]] ReceiveResult receive_header();

  /// Receive the message data and tail
  /// @return Receive result
  [[nodiscard]] ReceiveResult receive_message();

  jewels::memory::ObjectPtr<AbstractEPollManager> epoll_;
  std::pmr::string channel_name_;
  PublisherHandle publisher_;
  SubscriberHandle subscriber_;

  jewels::filesystem::FileDescriptor timer_fd_;
  std::optional<TcpSocket> socket_;
  jewels::networking::SocketAddress server_address_;

  State state_{State::disconnected};

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
  //    - If the state is searching_for_next_header then payload_ covers the header_search_buffer_
  //      until some data is read from the socket.
  std::span<std::byte> payload_;

  // Buffer for data left over from a prior header search.
  std::span<std::byte> header_search_remainder_;

  std::shared_ptr<TcpBridgeDiagnosticsCounters> diagnostics_counters_;
  clockwork_logging::LiteCompressor lite_compressor_;

  // Buffer used to collect the bytes for next message header candidate
  std::pmr::vector<std::byte> header_search_candidate_;

  /// Message receive buffer
  std::pmr::vector<std::byte> recv_buffer_;

  /// Header search buffer size
  static constexpr size_t header_search_buffer_size = 65536U;

  /// Header search buffer
  std::array<std::byte, header_search_buffer_size> header_search_buffer_{};

  /// Current message receive timestamp
  jewels::time::SyncTime current_receive_time_;

  /// Bridge client counters
  TcpBridgeClientServerCounters client_counters_;

  /// Last sequence number received
  uint64_t last_sequence_number_{};

  /// Minimum sequence number to accept
  uint64_t min_sequence_number_{};
};

} // namespace clockwork::pinion
