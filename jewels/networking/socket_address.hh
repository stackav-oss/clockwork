// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/filesystem/error_code.hh"
#include "jewels/std/expected.hh"

#include <cstdint>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <netinet/in.h>
#include <string>
#include <string_view>
#include <sys/socket.h>

namespace jewels::networking
{

/// A wrapper around `sockaddr`.
class SocketAddress
{
public:
  /// Construct an address from a hostname/IP and port.
  /// @tparam Allocator The allocator type for the string.
  /// @param[in] host A hostname or IP address.
  /// @param[in] port The port.
  /// @param[in] family The address family.
  template <class Allocator>
  static jewels::expected<SocketAddress, filesystem::ErrorCode> create(
    const std::basic_string<char, std::char_traits<char>, Allocator>& host,
    uint16_t port,
    sa_family_t family = AF_INET) noexcept;

  /// Construct an address from for IN_ADDR_ANY from a port.
  /// @param[in] port The port.
  [[nodiscard]] static SocketAddress create_any_address(uint16_t port) noexcept;

  /// Get the address as a sockaddr pointer.
  [[nodiscard]] const ::sockaddr* ptr() const noexcept;

  /// Get the address as a mutable sockaddr pointer.
  /// @note some APIs require a mutable ptr for the address.
  [[nodiscard]] ::sockaddr* mutable_ptr() noexcept;

  /// Get the port of this address.
  [[nodiscard]] ::in_port_t port() const noexcept;

  /// Get the size of the address in bytes.
  [[nodiscard]] static socklen_t byte_size();

private:
  /// Construct an address from a hostname/IP and port.
  /// @param[in] addr The socket address.
  explicit SocketAddress(sockaddr_in addr) noexcept;

  /// The underlying address object.
  ::sockaddr_in addr_;
};

/// A wrapper around `sockaddr_ll`.
class RawSocketAddress
{
public:
  /// Construct an address from a mac address.
  /// @param[in] address A mac address, in bytes.
  /// @param[in] interface_index The interface index.
  /// @param[in] protocol Ethernet protocol to use.
  static jewels::expected<RawSocketAddress, filesystem::ErrorCode>
  create(std::string_view address, int32_t interface_index, uint16_t protocol = ETH_P_ALL) noexcept;

  /// Get the address as a sockaddr pointer.
  [[nodiscard]] const ::sockaddr* ptr() const noexcept;

  /// Get the size of the address in bytes.
  [[nodiscard]] static socklen_t byte_size();

private:
  /// Construct an address from a hostname/IP and port.
  /// @param[in] addr The socket address.
  explicit RawSocketAddress(sockaddr_ll addr) noexcept;

  /// The underlying address object.
  ::sockaddr_ll addr_;
};

} // namespace jewels::networking

#include "jewels/networking/socket_address.inl"
