// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/std/expected.hh"

#include <string_view>
#include <system_error>

namespace clockwork::pinion
{

///
/// RAII container for a unix socket
/// This isn't intended to be a complete abstraction, but instead a convenience container than can factor out some
/// boilerplate from common operations
///
class UnixSocket
{
public:
  /// Creates a socket, binds it to the given address.
  /// @param name The name of the socket, in the abstract namespace
  static jewels::expected<UnixSocket, std::errc> create_bind(std::string_view name);

  /// Creates a socket and connects it to the given address
  /// @param name The name in the abstract namespace to connect to
  static jewels::expected<UnixSocket, std::errc> create_connect(std::string_view name);

  /// Create a UnixSocket from an existing descriptor
  explicit UnixSocket(jewels::filesystem::FileDescriptor sock) noexcept;

  /// Start listening on the socket
  /// @param backlog Size of the accept backlog
  [[nodiscard]] jewels::expected<void, std::errc> listen(int backlog);

  /// Gets the underlying file descriptor for the socket
  [[nodiscard]] int descriptor() const noexcept;

  /// Close the underlying socket.
  void close();

private:
  jewels::filesystem::FileDescriptor socket_;
};

} // namespace clockwork::pinion
