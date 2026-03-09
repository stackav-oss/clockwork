// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/detail/tcp_socket.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/networking/socket_address.hh"
#include "jewels/std/expected.hh"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <system_error>

namespace clockwork::pinion
{

TEST_CASE("TCP Socket | simple send and recv")
{
  const auto host = std::string{"127.0.0.1"};
  const auto server_addr = jewels::networking::SocketAddress::create(host, 0);
  const auto client_addr = jewels::networking::SocketAddress::create(host, 0);
  REQUIRE(server_addr);
  REQUIRE(client_addr);

  auto server = TcpSocket::create_listen(*server_addr, 1);
  REQUIRE(server);

  auto bound_server_addr = server->get_bound_address();
  REQUIRE(bound_server_addr);

  {
    const auto not_listening = jewels::networking::SocketAddress::create(host, bound_server_addr->port() + 1);
    REQUIRE(not_listening);
    auto client = TcpSocket::create_connect(*not_listening, *client_addr);
    CHECK(client == jewels::unexpected(std::errc::connection_refused));
  }

  auto client = TcpSocket::create_connect(*bound_server_addr, *client_addr);
  REQUIRE(client);

  // Allow address reuse to avoid spurious test failures when running this test
  // multiple times in quick succession.
  int enable = 1;
  REQUIRE(::setsockopt(server->descriptor(), SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) == 0);
  REQUIRE(::setsockopt(client->descriptor(), SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) == 0);

  const int accepted = ::accept(server->descriptor(), nullptr, nullptr);
  REQUIRE(accepted >= 0);

  struct iovec iov{};
  struct msghdr msg{
    .msg_name = nullptr,
    .msg_namelen = 0,
    .msg_iov = &iov,
    .msg_iovlen = 1,
    .msg_control = nullptr,
    .msg_controllen = 0,
    .msg_flags = 0,
  };
  auto payload = std::string{"hello! good job"};
  constexpr size_t buffer_max = 32;
  std::array<char, buffer_max> buffer{};

  // check 9999 -> 8888
  iov.iov_base = payload.data();
  iov.iov_len = payload.size();
  REQUIRE(::sendmsg(accepted, &msg, 0) == static_cast<ssize_t>(payload.size()));

  iov.iov_base = buffer.data();
  iov.iov_len = sizeof(buffer);
  auto recv_bytes = recvmsg(client->descriptor(), &msg, 0);
  CHECK(recv_bytes > 0);
  CHECK(std::string_view{buffer.data(), static_cast<size_t>(recv_bytes)} == payload);

  // check 8888 -> 9999
  iov.iov_base = payload.data();
  iov.iov_len = payload.size();
  REQUIRE(::sendmsg(client->descriptor(), &msg, 0) == static_cast<ssize_t>(payload.size()));

  buffer = {};
  iov.iov_base = buffer.data();
  iov.iov_len = sizeof(buffer);
  recv_bytes = recvmsg(accepted, &msg, 0);
  CHECK(recv_bytes > 0);
  CHECK(std::string_view{buffer.data(), static_cast<size_t>(recv_bytes)} == payload);
}

} // namespace clockwork::pinion
