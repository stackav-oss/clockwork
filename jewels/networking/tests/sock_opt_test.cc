// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/networking/sock_opt.hh"
#include "jewels/std/expected.hh"

#include <arpa/inet.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cerrno>
#include <netinet/in.h>
#include <sys/socket.h>

namespace jewels::networking
{
TEST_CASE("SockOpt")
{
  const jewels::filesystem::FileDescriptor file_desc{::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0)};
  // Try both on and off.
  const int value{GENERATE(0, 1)};
  REQUIRE(set_sock_opt<SockOption::so_reuse_address>(*file_desc, value));
  const auto read_value = get_sock_opt<SockOption::so_reuse_address>(*file_desc);
  REQUIRE(read_value);
  REQUIRE(*read_value == value);
}

TEST_CASE("Join Multicast Group")
{
  const jewels::filesystem::FileDescriptor file_desc{::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0)};
  REQUIRE_FALSE(get_sock_opt<SockOption::ip_add_membership>(*file_desc));
  ::ip_mreqn value{.imr_multiaddr = {}, .imr_address = {INADDR_ANY}, .imr_ifindex = 0};
  REQUIRE(::inet_pton(AF_INET, "127.0.0.1", &value.imr_address));

  // Should fail if we try to join a non-multicast address.
  REQUIRE(::inet_pton(AF_INET, "10.0.1.100", &value.imr_multiaddr));
  auto result = set_sock_opt<SockOption::ip_add_membership>(*file_desc, value);
  REQUIRE_FALSE(result);
  REQUIRE(result.error().value() == EINVAL);

  REQUIRE(::inet_pton(AF_INET, "239.22.0.2", &value.imr_multiaddr));
  REQUIRE(set_sock_opt<SockOption::ip_add_membership>(*file_desc, value));

  // ip(7) says IP_ADD_MEMBERSHIP is not gettable.
  REQUIRE_FALSE(get_sock_opt<SockOption::ip_add_membership>(*file_desc));

  auto loop_result = get_sock_opt<SockOption::ip_multicast_loop>(*file_desc);
  REQUIRE(loop_result);
  REQUIRE(*loop_result == 1);
  REQUIRE(set_sock_opt<SockOption::ip_multicast_loop>(*file_desc, 0));
  loop_result = get_sock_opt<SockOption::ip_multicast_loop>(*file_desc);
  REQUIRE(loop_result);
  REQUIRE(*loop_result == 0);
}

TEST_CASE("Set Multicast Interface")
{
  const jewels::filesystem::FileDescriptor file_desc{::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0)};
  ::in_addr iface{};
  REQUIRE(::inet_pton(AF_INET, "127.0.0.1", &iface));
  REQUIRE(set_sock_opt<SockOption::ip_multicast_if>(*file_desc, iface));
  auto read_iface = get_sock_opt<SockOption::ip_multicast_if>(*file_desc);
  REQUIRE(read_iface);
  REQUIRE(read_iface->s_addr == iface.s_addr);
}

TEST_CASE("Bind to Interface")
{
  const jewels::filesystem::FileDescriptor file_desc{::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0)};
  REQUIRE_FALSE(set_sock_opt<SockOption::so_bind_to_device>(*file_desc, "eth9999"));
  REQUIRE(set_sock_opt<SockOption::so_bind_to_device>(*file_desc, "lo"));
  auto get_result = get_sock_opt<SockOption::so_bind_to_device>(*file_desc);
  REQUIRE_FALSE(get_result);
  REQUIRE(get_result.error().value() == ENOPROTOOPT);
}

} // namespace jewels::networking
