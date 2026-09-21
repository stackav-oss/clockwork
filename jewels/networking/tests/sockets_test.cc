// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/networking/socket_address.hh"
#include "jewels/networking/sockets.hh"
#include "jewels/std/expected.hh"

#include <catch2/catch_test_macros.hpp>

#include <memory_resource> // IWYU pragma: keep
#include <string>
#include <sys/socket.h>

namespace jewels::networking
{
namespace
{

TEST_CASE("Get assigned port")
{
  SECTION("Success")
  {
    auto socket = jewels::filesystem::FileDescriptor{::socket(AF_INET, SOCK_STREAM, 0)};
    REQUIRE(socket);
    jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
    std::pmr::string ip_address{"127.0.0.1", memres};
    auto address = jewels::networking::SocketAddress::create(std::string{ip_address}, 0U);
    REQUIRE(::bind(*socket, address->ptr(), jewels::networking::SocketAddress::byte_size()) == 0);
    const auto port = get_assigned_port(socket);
    REQUIRE(port);
    REQUIRE(*port != 0U);
  }
  SECTION("Bad fd")
  {
    REQUIRE(!get_assigned_port({}));
  }
}

} // namespace
} // namespace jewels::networking
