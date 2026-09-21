// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0
// IWYU pragma: private, include "jewels/networking/socket_address.hh"
#pragma once

#include "jewels/networking/socket_address.hh"

#include "jewels/filesystem/error_code.hh"
#include "jewels/std/expected.hh"

#include <arpa/inet.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>

namespace jewels::networking
{

template <class Allocator>
jewels::expected<SocketAddress, filesystem::ErrorCode> SocketAddress::create(
  const std::basic_string<char, std::char_traits<char>, Allocator>& host, uint16_t port, sa_family_t family) noexcept
{
  ::sockaddr_in addr{};
  ::memset(&addr, 0, sizeof(addr));
  addr.sin_family = family;
  addr.sin_port = ::htons(port);

  if (::inet_pton(family, host.c_str(), &addr.sin_addr.s_addr) != 1)
  {
    return jewels::unexpected(filesystem::make_error_code(errno));
  }

  return {SocketAddress{addr}};
}

} // namespace jewels::networking
