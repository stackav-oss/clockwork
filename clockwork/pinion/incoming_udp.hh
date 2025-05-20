// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/abstract_epoll_manager.hh"
#include "clockwork/common/process_description.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/io_connection.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "clockwork/pinion/sock_opt.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/networking/sock_opt.hh"
#include "jewels/networking/socket_endpoint.hh"
#include "jewels/std/expected.hh"

#include <cstdint>
#include <memory>
#include <netinet/in.h>
#include <optional>
#include <string>
#include <sys/socket.h>
#include <vector>

namespace clockwork::pinion
{

/// A class to read UDP packets from a socket and publish into pinion.
/// While this can be used as standalone, it's intended to be used
/// alongside Clockwork's polling mechanisms.  See IncomingUdp and
/// BidirectionalUdp.
/// @note This class is the result of refactoring to extract core
/// UDP-to-pinion functionality from original IncomingUdp design so
/// that it can be reused by BidirectionalUdp.
template <class Payload>
class IncomingUdpImpl;

/// Specialization for Tachyon representations.
template <class Schema>
class IncomingUdpImpl<Tachyon<Schema>>
{
  using Msg = Tachyon<Schema>;

protected:
  /// Constructor
  /// @param host The address to connect to
  /// @param port The port to connect to
  IncomingUdpImpl(
    std::pmr::vector<::iovec>&& io_vecs,
    std::pmr::vector<::mmsghdr>&& mmsg_hdrs,
    std::pmr::vector<::sockaddr_in>&& msg_names,
    jewels::networking::SocketEndpoint socket_endpoint);

  /// Read message and publish to channel
  /// @param socket_fd File descriptor associated with socket to read
  void read_impl(int socket_fd);

  /// Connect the publisher
  /// @param publisher Publisher handle to be connected
  [[nodiscard]] jewels::expected<void, IoConnection::Error> connect_publisher_impl(pinion::PublisherHandle&& publisher);

  /// Register as an epoll callback so that it can be a callback when there are incoming packets
  /// @param manager Epoll manager to register with
  /// @param socket_fd File descriptor associated with the socket to be registered
  /// @param callback Object providing the callback
  jewels::expected<void, jewels::MonoError> register_with_impl(
    AbstractEPollManager& manager, int socket_fd, const std::shared_ptr<AbstractEPollCallback>& callback);

private:
  /// Reserve a new batch.  Pass in the publisher handle to avoid extra optional checks.
  jewels::expected<void, ReserveError> reserve_new_batch(pinion::PublisherHandle& publisher_handle);

  std::optional<pinion::PublisherHandle> publisher_;
  std::optional<pinion::BatchReservedSlot> reserved_batch_;
  std::pmr::vector<::iovec> io_vecs_;
  std::pmr::vector<::mmsghdr> mmsg_hdrs_;
  std::pmr::vector<::sockaddr_in> msg_names_;
  size_t slots_available_{0UL};
  jewels::networking::SocketEndpoint socket_endpoint_;
};

/// A class to read UDP packets from a socket and publish into pinion, with ability to be registed with Clockwork's
/// polling mechanisms.
/// @tparam Payload The message type for the connected channel.
template <class Payload>
class IncomingUdp;

/// Specialization for Tachyon representations.
template <class Schema>
// NOLINTNEXTLINE(fuchsia-multiple-inheritance) Needs to implement the interfaces and reuse functionality.
class IncomingUdp<Tachyon<Schema>> final : public IncomingUdpImpl<Tachyon<Schema>>,
                                           public EPollable,
                                           public AbstractEPollCallback,
                                           public IoConnection
{
  using Msg = Tachyon<Schema>;

public:
  /// Construct from an endpoint.
  /// @param memres A memory resource for allocating the object.
  /// @param socket_endpoint The address and port to connect to.
  /// @param batch_size The number of messages to read before publishing.
  /// @param sock_option_values A pack of socket options to set.
  template <jewels::networking::SockOption... options>
  static jewels::expected<jewels::memory::NonNullSharedPtr<IncomingUdp<Msg>>, jewels::filesystem::ErrorCode> try_make(
    jewels::memory::MemoryResource memres,
    jewels::networking::SocketEndpoint socket_endpoint,
    size_t batch_size,
    const SockOptionValue<options>&... sock_option_values);

  IncomingUdp(const IncomingUdp&) = delete;
  void operator=(const IncomingUdp&) = delete;

  IncomingUdp(IncomingUdp&&) noexcept = default;
  IncomingUdp& operator=(IncomingUdp&&) noexcept = default;

  ~IncomingUdp() noexcept final = default;

  /// Callback for the EPoll loop to read message from the socket and publish into pinion.
  void notify(AbstractEPollManager& /*epoll*/, int /*efd*/, uint32_t /*events*/) final;

  /// Read message from the socket and publish into pinion.
  void read();

  /// Get the socket file descriptor.
  [[nodiscard]] int fd() const;

  /// Register as an epoll callback
  [[nodiscard]] jewels::expected<void, jewels::MonoError> register_with(AbstractEPollManager& manager) final;

  /// Connect the publisher.
  [[nodiscard]] jewels::expected<void, IoConnection::Error> connect_publisher(pinion::PublisherHandle publisher) final;

private:
  /// Constructor
  /// @param file_descriptor The socket fd to be used for receiving data
  /// @param io_vecs Preallocated io_vec s for use with mmesg syscalls.
  /// @param mmsg_hdrs Preallocated mmsghdr s for use with mmesg syscalls.
  /// @param msg_names Preallocated socketaddr_in s for use with mmesg syscalls.
  /// @param socket_endpoint The address and port to connect to.
  IncomingUdp(
    jewels::filesystem::FileDescriptor&& file_descriptor,
    std::pmr::vector<::iovec>&& io_vecs,
    std::pmr::vector<::mmsghdr>&& mmsg_hdrs,
    std::pmr::vector<::sockaddr_in>&& msg_names,
    jewels::networking::SocketEndpoint socket_endpoint);

  jewels::filesystem::FileDescriptor file_descriptor_{};
};

} // namespace clockwork::pinion

#include "clockwork/pinion/incoming_udp.inl"
