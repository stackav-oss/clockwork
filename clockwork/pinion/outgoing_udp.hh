// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/process_description.hh" // IWYU pragma: keep
#include "clockwork/io/network_var_packet.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/io_connection.hh"
#include "clockwork/pinion/observer.hh"
#include "clockwork/pinion/sock_opt.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/networking/sock_opt.hh"
#include "jewels/networking/socket_address.hh"
#include "jewels/networking/socket_endpoint.hh"
#include "jewels/std/expected.hh"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace clockwork::pinion
{

// Tests for compatibility. We do not support NetworkVarPacket because the
// address/port fields are misleading for this class.
template <class Schema>
constexpr bool is_outgoing_compatible{true};

template <size_t payload_size>
constexpr bool is_outgoing_compatible<io::NetworkVarPacket<payload_size>>{false};
/// A class to read messages from a pinion subscriber and write to a
/// UDP socket.  While this can be used as standalone, it's intended
/// to be used alongside Clockwork's polling mechanisms.  See
/// OutgoingUdp and BidirectionalUdp.
/// @note This class is the result of refactoring to extract core
/// pinion-to-UDP functionality from original IncomingUdp design so
/// that it can be reused by BidirectionalUdp.
template <class Payload>
class OutgoingUdpImpl;

template <class Schema>
class OutgoingUdpImpl<Tachyon<Schema>>
{

  using Msg = Tachyon<Schema>;

protected:
  /// Constructor
  /// @param host The address and port to connect to
  /// @param address The address structure associated with the remote host
  /// @param holding_buffer Staging buffer
  OutgoingUdpImpl(
    jewels::networking::SocketEndpoint socket_endpoint,
    jewels::networking::SocketAddress address,
    jewels::memory::pmr_unique_ptr<Msg>&& holding_buffer);

  /// Write channel data to socket
  /// @param socket_fd File descriptor of socket to write to
  void write_impl(int socket_fd);

  /// Connect the subscriber
  /// @param subscriber Subscriber handle to be connected
  jewels::expected<void, IoConnection::Error> connect_subscriber_impl(pinion::SubscriberHandle& subscriber);

private:
  /// Helper function to write a specific range of messages to the socket.
  /// @param subscriber Take in a handle explicitly to avoid another optional check.
  /// @param available The range of messages to write.
  void write(pinion::SubscriberHandle& subscriber, std::ranges::subrange<BufferIterator> available, int socket_fd);

  /// A subscriber handle to read from.
  std::optional<pinion::SubscriberHandle> subscriber_;
  /// Socket address and port
  jewels::networking::SocketEndpoint socket_endpoint_;
  /// Socket address.
  jewels::networking::SocketAddress address_;
  /// A temporary buffer to copy into and write from.
  jewels::memory::pmr_unique_ptr<Msg> holding_buffer_;
  /// Current read cursor.
  pinion::BufferIterator next_to_consume_;
};

/// A class to read messages from a pinion subscriber and write to a UDP socket, with ability to be registed with
/// Clockwork's polling mechanisms.
/// @tparam Payload The message type for the connected channel.
template <class Payload>
class OutgoingUdp;

/// Specialization for Tachyon representations.
template <class Schema>
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)  Must implement these interfaces.
class OutgoingUdp<Tachyon<Schema>> final : public OutgoingUdpImpl<Tachyon<Schema>>,
                                           public Observer,
                                           public IoConnection,
                                           public std::enable_shared_from_this<OutgoingUdp<Tachyon<Schema>>>
{
  using Msg = Tachyon<Schema>;

  static_assert(is_outgoing_compatible<Schema>, "Schema is not compatible with OutgoingUdp");

public:
  /// Construct from an endpoint.
  /// @param memres A memory resource for allocating the object.
  /// @param host The address to connect to.
  /// @param port The port to connect to.
  /// @param sock_option_values A pack of socket options to set.
  template <jewels::networking::SockOption... options>
  static jewels::expected<jewels::memory::NonNullSharedPtr<OutgoingUdp<Msg>>, jewels::filesystem::ErrorCode> try_make(
    jewels::memory::MemoryResource memres,
    jewels::networking::SocketEndpoint socket_endpoint,
    const SockOptionValue<options>&... sock_option_values);

  OutgoingUdp(const OutgoingUdp&) = delete;
  void operator=(const OutgoingUdp&) = delete;

  OutgoingUdp(OutgoingUdp&&) noexcept = default;
  OutgoingUdp& operator=(OutgoingUdp&&) noexcept = default;

  ~OutgoingUdp() final = default;

  /// Callback for the EPoll loop to read messages from pinion and write to the socket.
  void notify(const Event& event) final;

  /// Read messages from the subscriber handle and write to the socket.
  void write();

  /// Get the socket file descriptor.
  [[nodiscard]] int fd() const;

  /// Connect the subscriber.
  [[nodiscard]] jewels::expected<jewels::memory::NonNullSharedPtr<pinion::Observer>, IoConnection::Error>
  connect_subscriber(pinion::SubscriberHandle subscriber) final;

private:
  /// Constructor
  /// @param file_descriptor The socket fd to be used for sending data
  /// @param host The address and port to connect to
  /// @param address The address structure associated with the remote host
  /// @param holding_buffer Staging buffer
  OutgoingUdp(
    jewels::filesystem::FileDescriptor&& file_descriptor,
    jewels::networking::SocketEndpoint socket_endpoint,
    jewels::networking::SocketAddress address,
    jewels::memory::pmr_unique_ptr<Msg>&& holding_buffer);

  /// File descriptor for the socket.
  jewels::filesystem::FileDescriptor file_descriptor_{};
};

} // namespace clockwork::pinion

#include "clockwork/pinion/outgoing_udp.inl"
