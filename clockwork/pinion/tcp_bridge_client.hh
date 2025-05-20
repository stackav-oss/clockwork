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
  explicit TcpBridgeClient(
    const jewels::memory::MemoryResource& memres,
    jewels::memory::ObjectPtr<AbstractEPollManager> epoll,
    std::string_view channel_name,
    PublisherHandle&& publisher,
    SubscriberHandle subscriber,
    jewels::filesystem::FileDescriptor&& timer_fd,
    TcpSocket&& socket,
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
  /// Read messages from the socket into a buffer slots.
  void receive_messages();

  /// Send an acknowledgment so the server knows we received the last message
  void send_acknowledgement();

  /// Set the state to disconnected and start a timer to reconnect to the server
  void set_reconnect_timer();

  /// Starts an async connect to the server
  void start_reconnect();

  /// Completes the async connect to the server
  void complete_reconnect();

  /// Return values for receive_from_socket
  WISE_ENUM_CLASS_MEMBER((ReceiveResult, uint8_t), (message_complete, 1), (keep_reading, 2), (input_consumed, 3))

  /// Process data read from the socket into the current iovec
  /// @return True if more data may be available, false if all data has been consumed
  [[nodiscard]] ReceiveResult receive_from_socket();

  jewels::memory::ObjectPtr<AbstractEPollManager> epoll_;
  std::pmr::string channel_name_;
  PublisherHandle publisher_;
  SubscriberHandle subscriber_;

  jewels::filesystem::FileDescriptor timer_fd_;
  std::optional<TcpSocket> socket_;
  jewels::networking::SocketAddress server_address_;

  enum class State : uint8_t
  {
    disconnected,
    connecting,
    idle,
    receiving_header,
    receiving_message
  };
  State state_{State::idle};

  TcpMessageHeader header_{};
  TcpMessageTail tail_{};

  // If a prior call to receive_message() only partially part of a full
  // payload, this member will hold offsets to the unwritten portion of the
  // reserved buffer slot. In the common case, this should be zeroed out.
  std::array<struct iovec, 3> iovecs_{};

  std::shared_ptr<TcpBridgeDiagnosticsCounters> diagnostics_counters_;
  clockwork_logging::LiteCompressor lite_compressor_;
  std::pmr::vector<std::byte> recv_buffer_;

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
