// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/networking/ifaddrs.hh"

#include "jewels/filesystem/error_code.hh"
#include "jewels/std/expected.hh"

#include <arpa/inet.h>
#include <ifaddrs.h>

#include <cerrno>
#include <netinet/in.h>
#include <string>
#include <string_view>
#include <sys/socket.h>

namespace jewels::networking
{

jewels::expected<std::string, filesystem::ErrorCode> lookup_interface_name(std::string_view addr_str)
{
  const std::string addr_copy(addr_str.begin(), addr_str.end());
  ::in_addr addr{};
  if (::inet_aton(addr_copy.c_str(), &addr) == 0)
  {
    return jewels::unexpected{filesystem::ErrorCode{EINVAL}};
  }
  ::ifaddrs* addrs{nullptr};
  if (::getifaddrs(&addrs) == -1)
  {
    return jewels::unexpected{filesystem::ErrorCode{errno}};
  }

  for (auto* addr_it = addrs; addr_it != nullptr; addr_it = addr_it->ifa_next)
  {
    if (addr_it->ifa_addr == nullptr || addr_it->ifa_addr->sa_family != AF_INET || addr_it->ifa_name == nullptr)
    {
      continue;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) c-style inheritance
    if (reinterpret_cast<::sockaddr_in*>(addr_it->ifa_addr)->sin_addr.s_addr == addr.s_addr)
    {
      std::string name(addr_it->ifa_name);
      freeifaddrs(addrs);
      return name;
    }
  }
  freeifaddrs(addrs);
  return jewels::unexpected{filesystem::ErrorCode{ENOENT}};
}

} // namespace jewels::networking
