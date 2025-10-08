// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/networking/socket_address.hh"

#include <arpa/inet.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <span>
#include <sys/socket.h>

namespace jewels::networking
{
jewels::expected<SocketAddress, filesystem::ErrorCode>
SocketAddress::create(const std::string& host, uint16_t port, sa_family_t family) noexcept
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

SocketAddress::SocketAddress(sockaddr_in addr) noexcept
  : addr_{addr}
{
}

const ::sockaddr* SocketAddress::ptr() const noexcept
{
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) - required to interface with libc networking functions
  return reinterpret_cast<const ::sockaddr*>(&addr_);
}

::sockaddr* SocketAddress::mutable_ptr() noexcept
{
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) - required to interface with libc networking functions
  return reinterpret_cast<::sockaddr*>(&addr_);
}

::in_port_t SocketAddress::port() const noexcept
{
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) - required to interface with libc networking functions
  return addr_.sin_port;
}

socklen_t SocketAddress::byte_size()
{
  return static_cast<socklen_t>(sizeof(sockaddr_in));
}

jewels::expected<RawSocketAddress, filesystem::ErrorCode>
RawSocketAddress::create(const std::string_view address, const int32_t interface_index, const int32_t protocol) noexcept
{
  constexpr auto mac_address_len{6};
  ::sockaddr_ll addr{};
  ::memset(&addr, 0, sizeof(addr));

  addr.sll_family = AF_PACKET;
  /// The network interface index, which can be looked up via if_nametoindex()
  addr.sll_ifindex = interface_index;
  addr.sll_halen = mac_address_len;
  addr.sll_protocol = htons(protocol);

  auto addr_view = std::span(addr.sll_addr);
  std::ranges::copy(address, addr_view.begin());

  return {RawSocketAddress{addr}};
}

RawSocketAddress::RawSocketAddress(sockaddr_ll addr) noexcept
  : addr_{addr}
{
}

const ::sockaddr* RawSocketAddress::ptr() const noexcept
{
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) - required to interface with libc networking functions
  return reinterpret_cast<const ::sockaddr*>(&addr_);
}

socklen_t RawSocketAddress::byte_size()
{
  return static_cast<socklen_t>(sizeof(sockaddr_ll));
}

} // namespace jewels::networking
