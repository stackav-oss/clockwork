// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/networking/socket_address.hh"
#include "jewels/std/expected.hh"

#include <arpa/inet.h>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <memory_resource> // IWYU pragma: keep
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>

namespace jewels::networking
{

TEMPLATE_TEST_CASE("SocketAddress", "", std::string, std::pmr::string)
{
  const TestType host{"127.0.0.1"};
  // It's okay to specify a port here.  We're not actually opening a socket so no risk of collisions.
  auto address = SocketAddress::create(host, uint16_t{123}, AF_INET);
  REQUIRE(address);
  REQUIRE(::ntohs(address->port()) == 123U);
  REQUIRE(address->ptr() == address->mutable_ptr());
  REQUIRE(address->byte_size() == sizeof(::sockaddr_in));
  std::array<char, INET_ADDRSTRLEN> buffer{};
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) Required for C struct sockaddr conversions.
  const auto* sin_addr = &reinterpret_cast<struct sockaddr_in const*>(address->ptr())->sin_addr;
  REQUIRE(::inet_ntop(AF_INET, sin_addr, buffer.data(), buffer.size()) == host);
}

} // namespace jewels::networking
