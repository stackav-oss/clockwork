// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/networking/socket_address.hh"
#include "jewels/std/expected.hh"

#include <system_error>

namespace clockwork::pinion
{

///
/// RAII container for a TCP socket
/// This isn't intended to be a complete abstraction, but instead a convience container than can factor out some
/// boilerplate from common operations
///
class TcpSocket
{
public:
  /// Creates a socket, binds it to the given address, and sets it to listen
  /// @param sockaddr The address and port to bind to
  /// @param backlog Size of the accept backlog
  static jewels::expected<TcpSocket, std::errc>
  create_listen(const jewels::networking::SocketAddress& sockaddr, int backlog);

  /// Creates a socket, binds to the given local address and connects it to the given remote address
  /// @param remote_addr The address and port to connect to
  /// @param local_addr The address and port to bind to
  static jewels::expected<TcpSocket, std::errc> create_connect(
    const jewels::networking::SocketAddress& remote_addr, const jewels::networking::SocketAddress& local_addr);

  /// Creates a socket, binds to the given local address and starts to connect it to the given remote address
  /// asynchronously
  /// @param remote_addr The address and port to connect to
  /// @param local_addr The address and port to bind to
  static jewels::expected<TcpSocket, std::errc> create_connect_async(
    const jewels::networking::SocketAddress& remote_addr, const jewels::networking::SocketAddress& local_addr);

  /// Create a TcpSocket from an existing descriptor
  explicit TcpSocket(jewels::filesystem::FileDescriptor sock) noexcept;

  /// Gets the underlying file descriptor for the socket
  [[nodiscard]] int descriptor() const noexcept;

  /// Get ownership of the underlying file descriptor for the socket
  [[nodiscard]] jewels::filesystem::FileDescriptor release_descriptor();

  /// Get the bound address.
  [[nodiscard]] jewels::expected<jewels::networking::SocketAddress, std::errc> get_bound_address() const noexcept;

private:
  jewels::filesystem::FileDescriptor socket_;
};

} // namespace clockwork::pinion
