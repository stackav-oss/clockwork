// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/networking/sockets.hh"

#include <arpa/inet.h>

#include <cerrno>
#include <netinet/in.h>
#include <sys/socket.h>

namespace jewels::networking
{

jewels::expected<uint16_t, jewels::filesystem::ErrorCode> get_assigned_port(const int file_descriptor)
{
  ::sockaddr_in assigned_addr{};
  uint32_t len{sizeof(assigned_addr)};
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) c-style inheritance
  if (::getsockname(file_descriptor, reinterpret_cast<::sockaddr*>(&assigned_addr), &len) != 0)
  {
    return jewels::unexpected{jewels::filesystem::make_error_code(errno)};
  }
  return ::ntohs(assigned_addr.sin_port);
}

jewels::expected<uint16_t, jewels::filesystem::ErrorCode>
get_assigned_port(const jewels::filesystem::FileDescriptor& file_descriptor)
{
  return get_assigned_port(*file_descriptor);
}

} // namespace jewels::networking
