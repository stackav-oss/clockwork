// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/sock_opt.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/networking/sock_opt.hh"
#include "jewels/std/expected.hh"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cerrno>
#include <climits>
#include <string>
#include <sys/socket.h>
#include <tuple>

namespace clockwork::pinion
{

TEST_CASE("so_receive_buffer")
{
  const jewels::filesystem::FileDescriptor file_desc{::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0)};
  REQUIRE(handle_sock_options(
    *file_desc, std::make_tuple(SockOptionValue<jewels::networking::SockOption::so_receive_buffer>{4096})));
}

TEST_CASE("so_reuse_address")
{
  const jewels::filesystem::FileDescriptor file_desc{::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0)};
  const int value{GENERATE(0, 1)};
  REQUIRE(handle_sock_options(
    *file_desc, std::make_tuple(SockOptionValue<jewels::networking::SockOption::so_reuse_address>{value})));
}

TEST_CASE("ip_add_membership")
{
  const jewels::filesystem::FileDescriptor file_desc{::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0)};
  // Should fail if we give bogus addresses.
  std::string evil(static_cast<size_t>(NAME_MAX + 1), 'x');
  auto result = handle_sock_options(
    *file_desc,
    std::make_tuple(
      SockOptionValue<jewels::networking::SockOption::ip_add_membership>{
        .group_address = {evil}, .local_address = "127.0.0.1"}));
  REQUIRE_FALSE(result);
  REQUIRE(result.error().value() == EINVAL);
  result = handle_sock_options(
    *file_desc,
    std::make_tuple(
      SockOptionValue<jewels::networking::SockOption::ip_add_membership>{
        .group_address = "239.22.0.2", .local_address = {evil}}));
  REQUIRE_FALSE(result);
  REQUIRE(result.error().value() == EINVAL);
  result = handle_sock_options(
    *file_desc,
    std::make_tuple(
      SockOptionValue<jewels::networking::SockOption::ip_add_membership>{
        .group_address = "no", .local_address = "127.0.0.1"}));
  REQUIRE_FALSE(result);
  REQUIRE(result.error().value() == EINVAL);
  result = handle_sock_options(
    *file_desc,
    std::make_tuple(
      SockOptionValue<jewels::networking::SockOption::ip_add_membership>{
        .group_address = "239.22.0.2", .local_address = "stop"}));
  REQUIRE_FALSE(result);
  REQUIRE(result.error().value() == EINVAL);

  // Should fail if we try to join a non-multicast address.
  REQUIRE_FALSE(handle_sock_options(
    *file_desc,
    std::make_tuple(
      SockOptionValue<jewels::networking::SockOption::ip_add_membership>{
        .group_address = "10.0.1.100", .local_address = "127.0.0.1"})));

  REQUIRE(handle_sock_options(
    *file_desc,
    std::make_tuple(
      SockOptionValue<jewels::networking::SockOption::ip_add_membership>{
        .group_address = "239.22.0.2", .local_address = "127.0.0.1"})));
}

TEST_CASE("ip_multicast_if")
{
  const jewels::filesystem::FileDescriptor file_desc{::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0)};
  // Should fail if we give bogus or long addresses.
  std::string evil(static_cast<size_t>(NAME_MAX + 1), 'x');
  auto result = handle_sock_options(
    *file_desc, std::make_tuple(SockOptionValue<jewels::networking::SockOption::ip_multicast_if>{{evil}}));
  REQUIRE_FALSE(result);
  REQUIRE(result.error().value() == EINVAL);
  result = handle_sock_options(
    *file_desc, std::make_tuple(SockOptionValue<jewels::networking::SockOption::ip_multicast_if>{"????"}));
  REQUIRE_FALSE(result);
  REQUIRE(result.error().value() == EINVAL);
  REQUIRE(handle_sock_options(
    *file_desc, std::make_tuple(SockOptionValue<jewels::networking::SockOption::ip_multicast_if>{"127.0.0.1"})));
}

TEST_CASE("so_bind_to_device")
{
  const jewels::filesystem::FileDescriptor file_desc{::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0)};
  REQUIRE_FALSE(handle_sock_options(
    *file_desc, std::make_tuple(SockOptionValue<jewels::networking::SockOption::so_bind_to_device>{"8.8.8.8"})));
  REQUIRE(handle_sock_options(
    *file_desc, std::make_tuple(SockOptionValue<jewels::networking::SockOption::so_bind_to_device>{"127.0.0.1"})));
}

} // namespace clockwork::pinion
