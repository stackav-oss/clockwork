// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/filesystem/error_code.hh"
#include "jewels/std/expected.hh"

#include <wise_enum.h>

#include <cstdint>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string_view>
#include <sys/socket.h>
#include <variant>

namespace jewels::networking
{

/// Socket option enumeration.
WISE_ENUM_CLASS(
  (SockOption, uint8_t),
  (so_receive_buffer, SO_RCVBUF),
  (so_reuse_address, SO_REUSEADDR),
  (so_bind_to_device, SO_BINDTODEVICE),
  (tcp_nodelay, TCP_NODELAY),
  (tcp_quickack, TCP_QUICKACK),
  (ip_add_membership, IP_ADD_MEMBERSHIP),
  (ip_multicast_if, IP_MULTICAST_IF),
  (ip_multicast_loop, IP_MULTICAST_LOOP))

// A strongly typed string view representing an address.
struct AddressView
{
  std::string_view address;
};

// A strongly typed string view representing an interface name.
struct InterfaceNameView
{
  std::string_view name;
};

namespace detail
{

/// Type to map the socket option to the data type.
/// @note This provides a default for most options, but also allows a
/// customization point for other options.
template <SockOption>
struct OptionValue
{
  /// Most are int.
  using Type = int;
};

/// Multicast group request. See ip(7).
template <>
struct OptionValue<SockOption::ip_add_membership>
{
  using Type = ::ip_mreqn;
};

/// Multicast interface association. See ip(7).
template <>
struct OptionValue<SockOption::ip_multicast_if>
{
  using Type = ::in_addr;
};

template <>
struct OptionValue<SockOption::so_bind_to_device>
{
  using Type = std::variant<AddressView, InterfaceNameView>;
};
} // namespace detail

/// Alias to determine the option value type.
template <SockOption option>
using OptionValue = typename detail::OptionValue<option>::Type;

/// Set the socket option.
/// @tparam option The option enum.
/// @param file_desc The file descriptor for the socket.
/// @param value The value to set the option to.
/// @return An error code on failure.
template <SockOption option>
jewels::expected<void, filesystem::ErrorCode> set_sock_opt(int file_desc, OptionValue<option> value);

/// Get the socket option.
/// @tparam option The option enum.
/// @param file_desc The file descriptor for the socket.
/// @return The current value or an error code on failure.
template <SockOption option>
jewels::expected<OptionValue<option>, filesystem::ErrorCode> get_sock_opt(int file_desc);

} // namespace jewels::networking

#include "jewels/networking/sock_opt.inl"
