// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "clockwork/common/abstract_epoll_manager.hh"
#include "clockwork/logging/lite_compressor.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/detail/tcp_socket.hh"
#include "clockwork/pinion/observer.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "clockwork/pinion/tcp_bridge_common.hh"
#include "clockwork/pinion/tcp_bridge_config.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/time/sync_time.hh"

#include <wise_enum.h>

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

WISE_ENUM_CLASS(
  (TcpBridgeServerMode, uint8_t),
  production,  // Production mode
  overrun_test // Overrun test mode, ignore notifies from the channel observer
)

///
/// A simple TCP server that exports messages from an ShmSubscriber to a remote
/// TCP client.
///
/// @param[in] memres Memmory resource
/// @param[in] epoll EPoll manager
/// @param[in] channel_name Channel name
/// @param[in] subscriber Pinion channel subscriber
/// @param[in] listen_socket Socket used to listen for new connections
/// @param[in] listen_port Port used to listen for new connections
/// @param[in] max_clients Maximum number of client connections
/// @param[in] diagnostics_counters Diagnostics counters
/// @param[in] mode Bridge server mode (production or unit test)
///
// NOLINTNEXTLINE(fuchsia-multiple-inheritance) Required to implement these interfaces.
struct TcpBridgeServer : public AbstractEPollCallback, public Observer
{
public:
  TcpBridgeServer(
    jewels::memory::MemoryResource memres,
    jewels::memory::ObjectPtr<AbstractEPollManager> epoll,
    std::string_view channel_name,
    SubscriberHandle&& subscriber,
    TcpSocket&& listen_socket,
    uint16_t listen_port,
    size_t max_clients,
    std::shared_ptr<TcpBridgeDiagnosticsCounters> diagnostics_counters,
    TcpBridgeServerMode mode = TcpBridgeServerMode::production);

  ~TcpBridgeServer() override = default;

  TcpBridgeServer() = delete;
  TcpBridgeServer(const TcpBridgeServer&) = delete;
  TcpBridgeServer& operator=(const TcpBridgeServer&) = delete;
  TcpBridgeServer(TcpBridgeServer&&) = default;
  TcpBridgeServer& operator=(TcpBridgeServer&&) = delete;

  /// Create a TCP brige server
  /// @param[in] memres Memory resource
  /// @param[in] config TCP bridge server configuration
  /// @param[in] maybe_channel Optional pinion channel pointer
  /// @param[in] subscriber Pinion subscriber handle
  /// @param[in] epoll EPoll manager pointer
  /// @param[in] diagnostics_counters TCP bridge diagnostics counters
  /// @return Shared pointer to the server or a null pointer on error
  /// @param[in] mode Bridge server mode (production or unit test)
  [[nodiscard]] static std::shared_ptr<TcpBridgeServer> make(
    jewels::memory::MemoryResource memres,
    const TcpBridgeServerConfigTap& config,
    SubscriberHandle&& subscriber,
    jewels::memory::ObjectPtr<AbstractEPollManager> epoll,
    const std::shared_ptr<TcpBridgeDiagnosticsCounters>& diagnostics_counters,
    TcpBridgeServerMode mode = TcpBridgeServerMode::production);

  /// Returns the file descriptor of the TCP socket.
  [[nodiscard]] int listen_fd() const;

  /// Handle an epoll event.
  void notify(AbstractEPollManager& epoll, int efd, uint32_t events) override;

  /// Gets a notification from the subscriber handle and forwards it to the
  /// remote end of the TCP socket.
  void notify(const Observer::Event& event) override;

  /// @return Channel name
  [[nodiscard]] std::string_view channel_name() const;

  /// Get and reset the bridge client counters
  /// @return Bridge client counters
  [[nodiscard]] TcpBridgeClientServerCounters get_and_reset_counters();

  /// Send a null header on any idle sockets that have not received an acknowledgement
  /// for the last message sent to avoid relying on tail loss recovery to detect that the
  /// last data segment in a message was dropped.
  void send_null_header_if_waiting_for_ack();

  /// Test whether the the server is waiting for an acknowledgement on any socket
  /// @return True if any socket is waiting for an acknowledsgement
  [[nodiscard]] bool is_waiting_for_ack() const;

  /// Get the TCP port number that the server is listening on.
  /// Used in unit tests.
  /// @return TCP port number
  [[nodiscard]] uint16_t listen_port() const noexcept;

private:
  /// Accept a new client.
  void accept_client(AbstractEPollManager& epoll);

  /// Try to find a hole in the client list for a new connection.
  [[nodiscard]] std::optional<size_t> find_empty_slot();

  struct Client : AbstractEPollCallback
  {
  public:
    /// Client constructor arguments
    struct ClientArgs
    {
      jewels::memory::MemoryResource memres;
      jewels::memory::ObjectPtr<AbstractEPollManager> epoll;
      std::string_view channel_name;
      jewels::filesystem::FileDescriptor client_fd;
      jewels::memory::ObjectPtr<SubscriberHandle> subscriber;
      std::shared_ptr<TcpBridgeDiagnosticsCounters> diagnostics_counters;
      std::shared_ptr<TcpBridgeClientServerCounters> server_counters;
    };

    explicit Client(ClientArgs args);

    ~Client() noexcept override = default;

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    Client(Client&&) = delete;
    Client& operator=(Client&&) = delete;

    /// Gets the underlying client socket descriptor.
    [[nodiscard]] int client_fd() const;

    /// Handle an epoll event.
    void notify(AbstractEPollManager& epoll, int efd, uint32_t events) override;

    /// Send new messages from the subscriber to the client.
    void send_messages();

    /// Receive any acknowledgement messages from the receiver
    void receive_acknowledgements();

    /// Send a null header if the socket is idle and has not received an acknowledgement
    /// for the last message sent to avoid relying on tail loss recovery to detect that the
    /// last data segment in a message was dropped.
    void send_null_header_if_waiting_for_ack();

    /// Test whether the the socket is idle and waiting for an acknowledgement
    /// @return True if the socket is waiting for an acknowledsgement
    [[nodiscard]] bool is_waiting_for_ack() const;

  private:
    /// Flush the pending payload.
    void send_pending_payload();

    jewels::memory::ObjectPtr<AbstractEPollManager> epoll_;
    std::string_view channel_name_;
    jewels::filesystem::FileDescriptor client_fd_;
    jewels::memory::ObjectPtr<SubscriberHandle> subscriber_;

    /// Null message header for kicking the receiver
    TcpMessageHeader null_header_{};

    /// Last message received on the channel
    BufferIterator last_message_{};

    // If a prior call to sendmsg() only partially sent its payload, this member
    // will hold the remainder so it can be sent later. In this case
    // last_message_ will also refer to the slot that contains the unfinished
    // message. In the common case, this should hold be empty
    std::span<std::byte> payload_;

    /// Bridge diagnostics counters
    std::shared_ptr<TcpBridgeDiagnosticsCounters> diagnostics_counters_;

    /// Bridge server counters
    std::shared_ptr<TcpBridgeClientServerCounters> server_counters_;

    /// Message compressor
    clockwork_logging::LiteCompressor lite_compressor_;

    /// Send buffer
    std::pmr::vector<std::byte> send_buffer_;

    /// Current message source commit timestamp
    int64_t current_source_commit_timestamp_{};

    /// Current message receive time
    jewels::time::SyncTime current_receive_time_;

    /// Current message size in bytes
    size_t current_message_size_{};

    /// Current message compression end time
    jewels::time::SyncTime current_compression_end_time_;

    /// Flag set when server is sending a null header
    bool sending_null_header_{false};

    /// Last sequence number sent while waiting for an acknowledgement
    std::optional<uint64_t> maybe_last_sequence_number_;
  };

  jewels::memory::MemoryResource memres_;
  jewels::memory::ObjectPtr<AbstractEPollManager> epoll_;
  std::pmr::string channel_name_;
  SubscriberHandle subscriber_;
  TcpSocket listen_socket_;
  uint16_t listen_port_;

  // Client list. This may have holes.
  std::pmr::vector<std::shared_ptr<Client>> clients_;

  /// Bridge diagnostics counters
  std::shared_ptr<TcpBridgeDiagnosticsCounters> diagnostics_counters_;

  /// Bridge server counters
  std::shared_ptr<TcpBridgeClientServerCounters> server_counters_;

  /// Bridge server mode (unit test or production)
  TcpBridgeServerMode mode_;
};

} // namespace clockwork::pinion
