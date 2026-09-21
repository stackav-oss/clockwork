// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/networking/sock_opt.hh"

#include "jewels/filesystem/error_code.hh"
#include "jewels/meta/overloaded.hh"
#include "jewels/networking/ifaddrs.hh"
#include "jewels/std/expected.hh"

#include <algorithm>
#include <array>
#include <cerrno>
#include <net/if.h>
#include <string>
#include <sys/socket.h>

namespace jewels::networking
{

template <>
jewels::expected<void, filesystem::ErrorCode>
set_sock_opt<SockOption::so_bind_to_device>(int file_desc, OptionValue<SockOption::so_bind_to_device> value)
{
  std::array<char, IFNAMSIZ> addr_buf{};
  const auto iface_name_result = std::visit(
    jewels::meta::Overloaded{
      [&addr_buf](InterfaceNameView value) -> jewels::expected<void, filesystem::ErrorCode>
      {
        if (value.name.size() > addr_buf.size())
        {
          return jewels::unexpected{filesystem::make_error_code(EINVAL)};
        }
        std::ranges::copy(value.name.begin(), value.name.end(), addr_buf.begin());

        return {};
      },
      [&addr_buf](AddressView value) -> jewels::expected<void, filesystem::ErrorCode>
      {
        const auto iface_name = jewels::networking::lookup_interface_name(value.address);
        if (!iface_name)
        {
          return jewels::unexpected{iface_name.error()};
        }
        if (iface_name->size() > addr_buf.size())
        {
          return jewels::unexpected{filesystem::make_error_code(EINVAL)};
        }
        std::ranges::copy(iface_name->begin(), iface_name->end(), addr_buf.begin());

        return {};
      }},
    value);

  if (!iface_name_result)
  {
    return iface_name_result;
  }

  const auto result = ::setsockopt(
    file_desc,
    detail::level<SockOption::so_bind_to_device>,
    static_cast<int>(SockOption::so_bind_to_device),
    static_cast<const void*>(addr_buf.data()),
    addr_buf.size());
  if (result != 0)
  {
    return jewels::unexpected(filesystem::make_error_code(errno));
  }
  return {};
}

template <>
jewels::expected<OptionValue<SockOption::so_bind_to_device>, filesystem::ErrorCode>
get_sock_opt<SockOption::so_bind_to_device>(int /*file_desc*/)
{
  return jewels::unexpected(filesystem::make_error_code(ENOPROTOOPT));
}

} // namespace jewels::networking
