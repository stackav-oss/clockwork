// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/sock_opt.hh"

#include "jewels/filesystem/error_code.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/meta/overloaded.hh"
#include "jewels/networking/sock_opt.hh"
#include "jewels/std/expected.hh"

#include <arpa/inet.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <netinet/in.h>
#include <sys/socket.h>

namespace clockwork::pinion
{

jewels::expected<void, jewels::filesystem::ErrorCode>
handle_sock_option(int file_desc, SockOptionValue<jewels::networking::SockOption::so_receive_buffer> value)
{
  if (auto result =
        jewels::networking::set_sock_opt<jewels::networking::SockOption::so_receive_buffer>(file_desc, value.value);
      !result)
  {
    jewels::log_cerr_error("Failed to set {} to {}", jewels::networking::SockOption::so_receive_buffer, value.value);
    return jewels::unexpected{result.error()};
  }
  auto get_result = jewels::networking::get_sock_opt<jewels::networking::SockOption::so_receive_buffer>(file_desc);
  if (!get_result)
  {
    jewels::log_cerr_error("Failed to get: {}", jewels::networking::SockOption::so_receive_buffer);
    return jewels::unexpected{get_result.error()};
  }
  if (*get_result < value.value)
  {
    jewels::log_cerr_error(
      "Value for {} was set, but current value of {} is smaller than the desired {}",
      jewels::networking::SockOption::so_receive_buffer,
      *get_result,
      value.value);
    // A failure here is assumed to be a bad input.
    return jewels::unexpected{jewels::filesystem::make_error_code(EINVAL)};
  }
  return {};
}

jewels::expected<void, jewels::filesystem::ErrorCode>
handle_sock_option(int file_desc, SockOptionValue<jewels::networking::SockOption::so_reuse_address> value)
{
  if (auto result =
        jewels::networking::set_sock_opt<jewels::networking::SockOption::so_reuse_address>(file_desc, value.value);
      !result)
  {
    jewels::log_cerr_error("Failed to set {} to {}", jewels::networking::SockOption::so_reuse_address, value.value);
    return jewels::unexpected{result.error()};
  }
  auto get_result = jewels::networking::get_sock_opt<jewels::networking::SockOption::so_reuse_address>(file_desc);
  if (!get_result)
  {
    jewels::log_cerr_error("Failed to get: {}", jewels::networking::SockOption::so_reuse_address);
    return jewels::unexpected{get_result.error()};
  }
  if (*get_result != value.value)
  {
    jewels::log_cerr_error(
      "Value for {} was set, but incorrect.  Expected {} but got {}.",
      jewels::networking::SockOption::so_reuse_address,
      value.value,
      *get_result);
    // A failure here is assumed to be a bad input.
    return jewels::unexpected{jewels::filesystem::make_error_code(EINVAL)};
  }
  return {};
}

jewels::expected<void, jewels::filesystem::ErrorCode>
handle_sock_option(int file_desc, SockOptionValue<jewels::networking::SockOption::so_bind_to_device> value)
{
  auto result =
    jewels::networking::set_sock_opt<jewels::networking::SockOption::so_bind_to_device>(file_desc, value.value);
  if (!result)
  {
    const std::string_view value_string_view = std::visit(
      jewels::meta::Overloaded{
        [](jewels::networking::AddressView underlying_value) { return underlying_value.address; },
        [](jewels::networking::InterfaceNameView underlying_value) { return underlying_value.name; }},
      value.value);
    jewels::log_cerr_error(
      "Failed to set {} to {}", jewels::networking::SockOption::so_bind_to_device, value_string_view);
  }
  return result;
}

jewels::expected<void, jewels::filesystem::ErrorCode>
handle_sock_option(int file_desc, SockOptionValue<jewels::networking::SockOption::ip_add_membership> value)
{
  ::ip_mreqn request{};
  std::array<char, NAME_MAX> addr_buf{};
  if (value.group_address.size() > addr_buf.size() || value.local_address.size() > addr_buf.size())
  {
    return jewels::unexpected{jewels::filesystem::make_error_code(EINVAL)};
  }

  std::ranges::copy(value.group_address.begin(), value.group_address.end(), addr_buf.begin());
  auto result = ::inet_pton(AF_INET, addr_buf.data(), &request.imr_multiaddr);
  if (result == 0)
  {
    return jewels::unexpected{jewels::filesystem::make_error_code(EINVAL)};
  }
  if (result == -1)
  {
    return jewels::unexpected(jewels::filesystem::make_error_code(errno));
  }

  addr_buf.fill('\0');
  std::ranges::copy(value.local_address.begin(), value.local_address.end(), addr_buf.begin());
  result = ::inet_pton(AF_INET, addr_buf.data(), &request.imr_address);
  if (result == 0)
  {
    return jewels::unexpected{jewels::filesystem::make_error_code(EINVAL)};
  }
  if (result == -1)
  {
    return jewels::unexpected(jewels::filesystem::make_error_code(errno));
  }

  auto set_result =
    jewels::networking::set_sock_opt<jewels::networking::SockOption::ip_add_membership>(file_desc, request);
  if (!set_result)
  {
    return jewels::unexpected(set_result.error());
  }

  return {};
}

jewels::expected<void, jewels::filesystem::ErrorCode>
handle_sock_option(int file_desc, SockOptionValue<jewels::networking::SockOption::ip_multicast_if> value)
{
  ::in_addr iface{};
  std::array<char, NAME_MAX> addr_buf{};
  if (value.interface_address.size() > addr_buf.size())
  {
    return jewels::unexpected{jewels::filesystem::make_error_code(EINVAL)};
  }
  std::ranges::copy(value.interface_address.begin(), value.interface_address.end(), addr_buf.begin());
  auto result = ::inet_pton(AF_INET, addr_buf.data(), &iface);
  if (result == 0)
  {
    return jewels::unexpected{jewels::filesystem::make_error_code(EINVAL)};
  }
  if (result == -1)
  {
    return jewels::unexpected(jewels::filesystem::make_error_code(errno));
  }
  return jewels::networking::set_sock_opt<jewels::networking::SockOption::ip_multicast_if>(file_desc, iface);
}

jewels::expected<void, jewels::filesystem::ErrorCode>
handle_sock_option(int file_desc, SockOptionValue<jewels::networking::SockOption::ip_multicast_loop> value)
{
  return jewels::networking::set_sock_opt<jewels::networking::SockOption::ip_multicast_loop>(file_desc, value.value);
}

} // namespace clockwork::pinion
