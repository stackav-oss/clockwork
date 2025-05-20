// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/abstract_epoll_manager.hh"
#include "clockwork/common/process_description.hh" // IWYU pragma: keep
#include "clockwork/io/network_var_packet.hh"      // IWYU pragma: keep
#include "clockwork/io/var_packet.hh"
#include "clockwork/pinion/incoming_udp.hh"
#include "clockwork/pinion/io_connection.hh"
#include "clockwork/pinion/observer.hh"
#include "clockwork/pinion/outgoing_udp.hh"
#include "clockwork/pinion/publisher_handle.hh"
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

#include <cstdint>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <vector>

namespace clockwork::pinion
{

/// A class (1) to read UDP packets from a socket and publish into pinion, and (2) to read from a pinion subscriber and
/// write to a UDP socket
template <class Payload>
class BidirectionalUdp;

template <class Schema>
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)  This class must implement all of these interfaces.
class BidirectionalUdp<Tachyon<Schema>> final : public IncomingUdpImpl<Tachyon<Schema>>,
                                                public OutgoingUdpImpl<Tachyon<Schema>>,
                                                public Observer,
                                                public EPollable,
                                                public AbstractEPollCallback,
                                                public IoConnection
{
  using Msg = Tachyon<Schema>;

public:
  /// Construct a BidirectionalUdp socket
  /// @param memres A memory resource for allocating the object
  /// @param local The host and port where this socket resides
  /// @param remote The host and port to communicate with
  /// @param sock_option_values A pack of socket options to set.
  template <jewels::networking::SockOption... options>
  static jewels::expected<jewels::memory::NonNullSharedPtr<BidirectionalUdp<Msg>>, jewels::filesystem::ErrorCode>
  try_make(
    jewels::memory::MemoryResource memres,
    jewels::networking::SocketEndpoint local,
    jewels::networking::SocketEndpoint remote,
    const SockOptionValue<options>&... sock_option_values);

  BidirectionalUdp(const BidirectionalUdp&) = delete;
  void operator=(const BidirectionalUdp&) = delete;

  BidirectionalUdp(BidirectionalUdp&&) = default;
  BidirectionalUdp& operator=(BidirectionalUdp&&) noexcept = default;

  ~BidirectionalUdp() noexcept final = default;

  /// Get the socket file descriptor.
  [[nodiscard]] int fd() const;

  /// Register as an epoll callback so that it can be a callback when there are incoming packets
  /// @note Overrrides Epollable::register_with
  [[nodiscard]] jewels::expected<void, jewels::MonoError> register_with(AbstractEPollManager& manager) final;

  /// Callback function for the epoll loop to read message from the socket and publish into pinion.
  /// @note Overrides AbstractEPollCallback::notify
  void notify(AbstractEPollManager& epoll, int efd, uint32_t events) final;

  /// Read message from the socket and publish into pinion.
  void read();

  /// Connect the publisher. The socket connects to an "incoming" channel onto which it'll write incoming packets.
  [[nodiscard]] jewels::expected<void, IoConnection::Error> connect_publisher(pinion::PublisherHandle publisher) final;

  /// Callback for the EPoll loop to read messages from pinion and write to the socket.  Socket is an Observer.
  /// @note Overrides Observer::notify
  void notify(const Event& event) final;

  /// Read messages from the subscriber handle and write to the socket.
  void write();

  /// Connect the subscriber. The socket subscribes to an "outgoing" channel from which it gets payload.
  [[nodiscard]] jewels::expected<jewels::memory::NonNullSharedPtr<pinion::Observer>, IoConnection::Error>
  connect_subscriber(pinion::SubscriberHandle subscriber) final;

private:
  /// Constructor
  /// @param file_descriptor The socket fd to be used for sending and receiving data
  /// @param local The host and port where this socket resides
  /// @param remote The host and port to communicate with
  /// @param remote_address The address structure associated with the remote host
  /// @param io_vecs Preallocated io_vec s for use with mmesg syscalls.
  /// @param mmsg_hdrs Preallocated mmsghdr s for use with mmesg syscalls.
  /// @param msg_names Preallocated socketaddr_in s for use with mmesg syscalls.
  /// @param holding_buffer Staging buffer
  BidirectionalUdp(
    jewels::filesystem::FileDescriptor&& file_descriptor,
    jewels::networking::SocketEndpoint local,
    jewels::networking::SocketEndpoint remote,
    jewels::networking::SocketAddress remote_address,
    std::pmr::vector<::iovec>&& io_vecs,
    std::pmr::vector<::mmsghdr>&& mmsg_hdrs,
    std::pmr::vector<::sockaddr_in>&& msg_names,
    jewels::memory::pmr_unique_ptr<Msg>&& holding_buffer);

  jewels::filesystem::FileDescriptor file_descriptor_{};
};
} // namespace clockwork::pinion

#include "clockwork/pinion/bidirectional_udp.inl"
