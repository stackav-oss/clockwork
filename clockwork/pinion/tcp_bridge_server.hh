// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "clockwork/common/abstract_epoll_manager.hh"
#include "clockwork/logging/lite_compressor.hh"
#include "clockwork/pinion/detail/tcp_socket.hh"
#include "clockwork/pinion/observer.hh"
#include "clockwork/pinion/slot_ref.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "clockwork/pinion/tcp_bridge_common.hh"
#include "clockwork/pinion/tcp_bridge_config_clk_cc.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/time/sync_time.hh"

#include <wise_enum.h>

#include <atomic>
#include <condition_variable>
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
/// A simple TCP server that exports messages from an ShmSubscriber to a remote
/// TCP client.
///
// NOLINTNEXTLINE(fuchsia-multiple-inheritance) Required to implement these interfaces.
struct TcpBridgeServer : public AbstractEPollCallback, public Observer
{
public:
  WISE_ENUM_CLASS_MEMBER(
    (Mode, uint8_t),
    production,  // Production mode
    overrun_test // Overrun test mode, ignore notifies from the channel observer
  )

  /// TcpBridgeServer constructor parameters
  struct TcpBridgeServerParams
  {
    /// Memmory resource
    jewels::memory::MemoryResource memres;
    /// EPoll manager
    jewels::memory::ObjectPtr<AbstractEPollManager> epoll;
    /// Channel name
    std::string_view channel_name;
    /// True if the channel holds bulk data
    bool is_bulk_data;
    /// Pinion channel subscriber
    SubscriberHandle subscriber;
    /// Socket used to listen for new connections
    TcpSocket listen_socket;
    /// Port used to listen for new connections
    uint16_t listen_port;
    /// Maximum number of client connections
    size_t max_clients;
    /// Diagnostics counter state
    std::shared_ptr<TcpBridgeDiagnosticsState> diagnostics_state;
    /// Bridge server mode
    Mode mode;
  };

  /// Constructor
  /// @param[in] params TCP bridge server parameters
  explicit TcpBridgeServer(TcpBridgeServerParams& params);

  ~TcpBridgeServer() override = default;

  TcpBridgeServer() = delete;
  TcpBridgeServer(const TcpBridgeServer&) = delete;
  TcpBridgeServer& operator=(const TcpBridgeServer&) = delete;
  TcpBridgeServer(TcpBridgeServer&&) = default;
  TcpBridgeServer& operator=(TcpBridgeServer&&) = delete;

  /// Create a TCP brige server
  /// @param[in] memres Memory resource
  /// @param[in] config TCP bridge server configuration
  /// @param[in] subscriber Pinion subscriber handle
  /// @param[in] epoll EPoll manager pointer
  /// @param[in] diagnostics_state TCP bridge diagnostics counter state
  /// @param[in] mode Bridge server mode (production or overrun test)
  /// @return Shared pointer to the server or a null pointer on error
  [[nodiscard]] static std::shared_ptr<TcpBridgeServer> make(
    jewels::memory::MemoryResource memres,
    const Tappy<TcpBridgeServerConfig>& config,
    SubscriberHandle subscriber,
    jewels::memory::ObjectPtr<AbstractEPollManager> epoll,
    std::shared_ptr<TcpBridgeDiagnosticsState> diagnostics_state,
    Mode mode = Mode::production);

  /// Returns the file descriptor of the TCP socket.
  [[nodiscard]] int listen_fd() const;

  /// Handle an epoll event.
  void notify(AbstractEPollManager& epoll, int efd, uint32_t events) override;

  /// Handle a notification for a new message on the subscriber handle
  void notify(const Observer::Event& event) override;

  /// Notify the clients of a new message on the subscriber handle
  void forced_notify();

  /// @return Channel name
  [[nodiscard]] std::string_view channel_name() const;

  /// Get and reset the bridge client counters
  /// @return Bridge client counters
  [[nodiscard]] TcpBridgeClientServerCounters get_and_reset_counters();

  /// Get the TCP port number that the server is listening on.
  /// Used in unit tests.
  /// @return TCP port number
  [[nodiscard]] uint16_t listen_port() const noexcept;

  /// Get the number of client connections
  /// @return Number of client connections
  [[nodiscard]] size_t get_num_clients() const noexcept;

private:
  /// Accept a new client.
  void accept_client();

  /// Try to find a hole in the client list for a new connection.
  /// @return The index of the empty slot on success
  [[nodiscard]] std::optional<size_t> find_empty_slot();

  /// Class to handle a connection to a bridge client
  class Client
  {
  public:
    /// Client constructor arguments
    struct ClientArgs
    {
      jewels::memory::MemoryResource memres;
      std::string_view channel_name;
      bool is_bulk_data;
      jewels::filesystem::FileDescriptor client_fd;
      jewels::memory::ObjectPtr<SubscriberHandle> subscriber;
      std::shared_ptr<TcpBridgeDiagnosticsState> diagnostics_state;
      std::shared_ptr<std::mutex> server_counters_mutex;
      std::shared_ptr<TcpBridgeClientServerCounters> server_counters;
      TcpBridgeServer::Mode mode;
    };

    explicit Client(ClientArgs args);

    ~Client();

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    Client(Client&&) = delete;
    Client& operator=(Client&&) = delete;

    /// Signal the worker to stop running
    void request_stop();

    /// Test whether the worker is running
    [[nodiscard]] bool is_running() const;

    /// Gets the underlying client socket descriptor.
    [[nodiscard]] int client_fd() const;

    /// Notify the worker of an epoll event on the bridge channel
    void notify();

  private:
    /// Worker thread main
    void worker_thread_main();

    /// Send new messages from the subscriber to the client.
    void send_messages();

    /// Receive any acknowledgement messages from the receiver
    void receive_acknowledgements();

    /// Send a null header to poke the client into sending an acknowledgement
    void send_null_header();

    /// Test whether the the socket is idle and waiting for an acknowledgement
    /// @return True if the socket is waiting for an acknowledsgement
    [[nodiscard]] bool is_waiting_for_ack() const;

    /// Send a payload chunkto the client
    /// @param[in] payload Payload to send
    /// @return True if the payload was sent
    [[nodiscard]] bool send_payload_chunk(std::span<std::byte> payload);

    /// Update the counters after sending a message
    void update_counters_for_message();

    /// Send a payload to the client
    /// @param[in] payload Payload to send
    /// @param[in] payload_type Payload type
    void send_payload(std::span<std::byte> payload, PayloadType payload_type);

    /// Break a payload up into checks and spread out transmission up to the transmission deadline
    /// @param[in] payload Payload to send
    /// @param[in] payload_type Payload type
    /// @param[in] transmission_deadline Payload transmission deadline
    void send_bulk_payload(
      std::span<std::byte> payload, PayloadType payload_type, jewels::time::SyncTime transmission_deadline);

    /// Channel name
    std::string_view channel_name_;

    /// Flag indicating whether the channel holds bulk data
    bool is_bulk_data_;

    /// Client file descriptor
    jewels::filesystem::FileDescriptor client_fd_;

    /// Bridged channel subscriber
    jewels::memory::ObjectPtr<SubscriberHandle> subscriber_;

    /// Null message header for kicking the receiver
    TcpMessageHeader null_header_{};

    /// Last message received on the channel
    SlotRef last_message_{};

    /// Bridge diagnostics counter state
    std::shared_ptr<TcpBridgeDiagnosticsState> diagnostics_state_;

    /// Bridge server counters mutex
    std::shared_ptr<std::mutex> server_counters_mutex_;

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

    /// Last sequence number sent that has not been ackknowledged
    std::optional<uint64_t> maybe_unacked_sequence_number_;

    /// Last sequence number sent (set to zero if no messages have been sent)
    uint64_t last_sequence_number_{};

    /// Last message transmit time
    jewels::time::SteadyTime last_send_time_;

    /// Worker is running flag
    std::atomic<bool> worker_is_running_{true};

    /// Worker stop requested flag
    bool worker_stop_requested_{false};

    /// Worker thread notify flag
    bool worker_notify_flag_{false};

    /// Worker thread mutex
    std::mutex worker_mutex_;

    /// Worker thread condition variable
    std::condition_variable worker_cv_;

    /// Worker thread
    std::thread worker_thread_;

    /// Bridge server mode
    Mode mode_;
  };

  /// Memory resource
  jewels::memory::MemoryResource memres_;

  /// EPoll manager
  jewels::memory::ObjectPtr<AbstractEPollManager> epoll_;

  /// Channel name
  std::pmr::string channel_name_;

  /// Flag indicating whether the channel holds bulk data
  bool is_bulk_data_;

  /// Pinion subscriber
  SubscriberHandle subscriber_;

  /// Listen socket
  TcpSocket listen_socket_;

  /// Listen port
  uint16_t listen_port_;

  // Client list. This may have holes.
  std::pmr::vector<std::shared_ptr<Client>> clients_;

  /// Bridge diagnostics counter state
  std::shared_ptr<TcpBridgeDiagnosticsState> diagnostics_state_;

  /// Bridge server counters mutex
  std::shared_ptr<std::mutex> server_counters_mutex_;

  /// Bridge server counters
  std::shared_ptr<TcpBridgeClientServerCounters> server_counters_;

  /// Bridge sever mode
  Mode mode_;
};

} // namespace clockwork::pinion
