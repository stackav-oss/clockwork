// IWYU pragma: private, include "jewels/networking/sock_opt.hh"
#pragma once

#include "jewels/networking/sock_opt.hh"

#include "jewels/filesystem/error_code.hh"
#include "jewels/std/expected.hh"

#include <cerrno>
#include <netinet/in.h>
#include <sys/socket.h>

namespace jewels::networking
{

namespace detail
{

template <SockOption option>
inline constexpr jewels::MonoError level{};

template <>
inline constexpr int level<SockOption::so_receive_buffer> = SOL_SOCKET;

template <>
inline constexpr int level<SockOption::so_reuse_address> = SOL_SOCKET;

template <>
inline constexpr int level<SockOption::so_bind_to_device> = SOL_SOCKET;

template <>
inline constexpr int level<SockOption::tcp_nodelay> = IPPROTO_TCP;

template <>
inline constexpr int level<SockOption::tcp_quickack> = IPPROTO_TCP;

template <>
inline constexpr int level<SockOption::ip_add_membership> = IPPROTO_IP;

template <>
inline constexpr int level<SockOption::ip_multicast_if> = IPPROTO_IP;

template <>
inline constexpr int level<SockOption::ip_multicast_loop> = IPPROTO_IP;
} // namespace detail

// specializations for SO_BINDTODEVICE, since its value type has to be handled
// differently.
template <>
jewels::expected<void, filesystem::ErrorCode>
set_sock_opt<SockOption::so_bind_to_device>(int file_desc, OptionValue<SockOption::so_bind_to_device> value);
template <>
jewels::expected<OptionValue<SockOption::so_bind_to_device>, filesystem::ErrorCode>
get_sock_opt<SockOption::so_bind_to_device>(int /*file_desc*/);

template <SockOption option>
jewels::expected<void, filesystem::ErrorCode> set_sock_opt(int file_desc, OptionValue<option> value)
{
  const auto result = ::setsockopt(
    file_desc, detail::level<option>, static_cast<int>(option), static_cast<const void*>(&value), sizeof(value));
  if (result != 0)
  {
    return jewels::unexpected(filesystem::make_error_code(errno));
  }
  return {};
}

template <SockOption option>
jewels::expected<OptionValue<option>, filesystem::ErrorCode> get_sock_opt(int file_desc)
{
  OptionValue<option> value{};
  ::socklen_t read_size{sizeof(value)};
  const auto result =
    ::getsockopt(file_desc, detail::level<option>, static_cast<int>(option), static_cast<void*>(&value), &read_size);
  if (result != 0)
  {
    return jewels::unexpected(filesystem::make_error_code(errno));
  }
  if (read_size != sizeof(value))
  {
    return jewels::unexpected(filesystem::make_error_code(EINVAL));
  }
  return {value};
}

} // namespace jewels::networking
