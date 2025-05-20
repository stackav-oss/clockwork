// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/filesystem/error_code.hh"
#include "jewels/meta/concepts.hh"
#include "jewels/networking/sock_opt.hh"
#include "jewels/std/expected.hh"

#include <string_view>
#include <tuple>

namespace clockwork::pinion
{

/// A type holding both the option and the option value.
/// @tparam The option enumeration.
template <jewels::networking::SockOption option>
struct SockOptionValue
{
  /// The option value.
  jewels::networking::OptionValue<option> value;
};

/// Value type for IP_ADD_MEMBERSHIP.
template <>
struct SockOptionValue<jewels::networking::SockOption::ip_add_membership>
{
  /// The multicast group to join.
  std::string_view group_address;
  /// The local unicast address to join with.
  std::string_view local_address;
};

/// Value type for IP_MULTICAST_IF.
template <>
struct SockOptionValue<jewels::networking::SockOption::ip_multicast_if>
{
  /// The address of the interface to use.
  std::string_view interface_address;
};

/// A concept to check if a socket option is supported by the pinion sockets.
template <jewels::networking::SockOption option>
concept SupportedSockOption = requires(int file_desc, SockOptionValue<option> value) {
  {
    handle_sock_option(file_desc, value)
  } -> jewels::meta::SameAs<jewels::expected<void, jewels::filesystem::ErrorCode>>;
};

/// Set and validate the SO_RECVBUF option.
/// @param value The value to set the receive buffer to.
/// @return An error code on failure.
jewels::expected<void, jewels::filesystem::ErrorCode>
handle_sock_option(int file_desc, SockOptionValue<jewels::networking::SockOption::so_receive_buffer> value);

/// Set and validate the SO_REUSEADDR option.
/// @param value The value to set the receive buffer to.
/// @return An error code on failure.
jewels::expected<void, jewels::filesystem::ErrorCode>
handle_sock_option(int file_desc, SockOptionValue<jewels::networking::SockOption::so_reuse_address> value);

/// Set the SO_BINDTODEVICE option.
/// @param value The name of the interface to bind to.
/// @return An error code on failure.
jewels::expected<void, jewels::filesystem::ErrorCode>
handle_sock_option(int file_desc, SockOptionValue<jewels::networking::SockOption::so_bind_to_device> value);

/// Set the IP_ADD_MEMBERSHIP option.
/// @param value The group parameters.
/// @return An error code on failure.
jewels::expected<void, jewels::filesystem::ErrorCode>
handle_sock_option(int file_desc, SockOptionValue<jewels::networking::SockOption::ip_add_membership> value);

/// Set the IP_MULTICAST_IF option.
/// @param value The interface to use.
/// @return An error code on failure.
jewels::expected<void, jewels::filesystem::ErrorCode>
handle_sock_option(int file_desc, SockOptionValue<jewels::networking::SockOption::ip_multicast_if> value);

/// Set the IP_MULTICAST_LOOP option.
/// @param value The flag value.
/// @return An error code on failure.
jewels::expected<void, jewels::filesystem::ErrorCode>
handle_sock_option(int file_desc, SockOptionValue<jewels::networking::SockOption::ip_multicast_loop> value);

/// Handle a tuple of socket options.
/// @tparam options A pack of socket options.
/// @param values A tuple of values for each option.
/// @return An error code on failure.
template <jewels::networking::SockOption... options>
  requires(SupportedSockOption<options> && ...)
jewels::expected<void, jewels::filesystem::ErrorCode>
handle_sock_options(int file_desc, const std::tuple<SockOptionValue<options>...>& values);

} // namespace clockwork::pinion

#include "clockwork/pinion/sock_opt.inl"
