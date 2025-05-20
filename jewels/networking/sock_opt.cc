// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/networking/sock_opt.hh"

#include "jewels/filesystem/error_code.hh"
#include "jewels/std/expected.hh"

#include <algorithm>
#include <array>
#include <cerrno>
#include <net/if.h>
#include <sys/socket.h>

namespace jewels::networking
{

template <>
jewels::expected<void, filesystem::ErrorCode>
set_sock_opt<SockOption::so_bind_to_device>(int file_desc, OptionValue<SockOption::so_bind_to_device> value)
{
  std::array<char, IFNAMSIZ> addr_buf{};
  if (value.size() > addr_buf.size())
  {
    return jewels::unexpected{filesystem::make_error_code(EINVAL)};
  }
  std::ranges::copy(value.begin(), value.end(), addr_buf.begin());
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
