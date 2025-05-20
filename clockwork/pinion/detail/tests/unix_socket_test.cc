// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/detail/unix_socket.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <random>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/un.h>
#include <system_error>
#include <type_traits>
#include <vector>

namespace clockwork::pinion
{
namespace
{
struct Tag
{
};

std::string random_string(size_t length)
{
  constexpr std::string_view chars = "0123456789";
  std::random_device random_device;
  std::mt19937 rng(random_device());
  std::uniform_int_distribution<size_t> distrib(0, chars.length() - 1);
  std::string result;
  result.reserve(length);
  for (size_t i = 0; i < length; ++i)
  {
    result += chars.at(distrib(rng));
  }
  return result;
}
} // namespace

TEST_CASE("Unix Socket | toy connect and send test")
{
  const std::string socket_name = jewels::Uuid<Tag>::random_uuid().to_string();

  auto listener = UnixSocket::create_bind(socket_name);
  REQUIRE(listener);
  REQUIRE(listener->listen(1));

  auto connector0 = UnixSocket::create_connect(socket_name + "x");
  CHECK(connector0 == jewels::unexpected(std::errc::connection_refused));

  auto connector1 = UnixSocket::create_connect(socket_name);
  REQUIRE(connector1);

  const int accepted1 = ::accept(listener->descriptor(), nullptr, nullptr);
  CHECK(accepted1 >= 0);

  auto connector2 = UnixSocket::create_connect(socket_name);
  REQUIRE(connector2);

  const int accepted2 = ::accept(listener->descriptor(), nullptr, nullptr);
  CHECK(accepted2 >= 0);

  constexpr size_t buffer_max = 16;
  std::vector<std::byte> buffer{buffer_max, std::byte{99}};
  struct iovec buffer_iovec{.iov_base = buffer.data(), .iov_len = buffer.size()};
  struct msghdr buffer_msg{
    .msg_name = nullptr,
    .msg_namelen = 0,
    .msg_iov = &buffer_iovec,
    .msg_iovlen = 1,
    .msg_control = nullptr,
    .msg_controllen = 0,
    .msg_flags = 0,
  };

  ssize_t nnet = 0;
  buffer[0] = std::byte{4};
  buffer_iovec.iov_len = 1;
  nnet = sendmsg(accepted1, &buffer_msg, 0);
  CHECK(nnet == 1);
  buffer[0] = std::byte{5};
  buffer[1] = std::byte{1};
  buffer_iovec.iov_len = 2;
  nnet = sendmsg(accepted1, &buffer_msg, 0);
  CHECK(nnet == 2);

  buffer_iovec.iov_base = buffer.data();
  buffer_iovec.iov_len = buffer.size();
  nnet = recvmsg(connector1->descriptor(), &buffer_msg, 0);
  CHECK(nnet == 1);
  CHECK(buffer[0] == std::byte{4});

  buffer_iovec.iov_base = buffer.data();
  buffer_iovec.iov_len = buffer.size();
  nnet = recvmsg(connector1->descriptor(), &buffer_msg, 0);
  CHECK(nnet == 2);
  CHECK(buffer[0] == std::byte{5});
  CHECK(buffer[1] == std::byte{1});
}

TEST_CASE("Unix Socket | name too big")
{
  constexpr size_t max_name = 108;
  // Just as a sanity check, validate that the system defined address size is what it should be
  static_assert(sizeof(std::declval<struct sockaddr_un>().sun_path) == max_name);
  SECTION("exact size")
  {
    std::string name = random_string(max_name - 1);
    auto sock = UnixSocket::create_bind(name);
    REQUIRE(sock);
    REQUIRE(sock->listen(2));
    // Confirm that the socket is available at $NAME
    auto conn1 = UnixSocket::create_connect(name);
    CHECK(conn1);
    // ...but not $NAME with the last character truncated
    name.resize(name.size() - 1);
    auto conn2 = UnixSocket::create_connect(name);
    CHECK(!conn2);
  }
  SECTION("too big")
  {
    const std::string name = random_string(max_name);
    auto sock = UnixSocket::create_bind(name);
    REQUIRE(!sock);
    CHECK(sock.error() == std::errc::filename_too_long);
  }
}

} // namespace clockwork::pinion
