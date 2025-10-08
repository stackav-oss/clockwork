// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/process_description.hh" // IWYU pragma: keep
#include "clockwork/io/var_packet.hh"
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/bidirectional_udp.hh"
#include "clockwork/pinion/detail/socket_payload.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/incoming_udp.hh"
#include "clockwork/pinion/outgoing_udp.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/sock_opt.hh"
#include "clockwork/pinion/tests/support/sockets.hh"
#include "jewels/container/compare.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_shared_ptr.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/networking/sock_opt.hh"
#include "jewels/networking/socket_address.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>
#include <endian.h>

#include <array>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>

namespace clockwork::pinion
{

TEST_CASE("Multicast Send/Recv")
{
  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  const std::pmr::string multicast_group{"239.22.0.2"};
  auto maybe_incoming = IncomingUdp<Tachyon<io::VarPacket<sizeof(uint32_t)>>>::try_make(
    memres,
    {},
    {.host = multicast_group, .port = 0U},
    1UL,
    SockOptionValue<jewels::networking::SockOption::ip_add_membership>{
      .group_address = {multicast_group}, .local_address = "127.0.0.1"});
  REQUIRE(maybe_incoming);

  auto& incoming_socket = *maybe_incoming;
  const auto incoming_assigned_addr = support::get_assigned_addr(incoming_socket->fd());
  REQUIRE(incoming_assigned_addr);
  const uint16_t port{support::get_port(*incoming_assigned_addr)};

  auto maybe_outgoing = OutgoingUdp<Tachyon<io::VarPacket<sizeof(uint32_t)>>>::try_make(
    memres,
    {},
    {.host = multicast_group, .port = port},
    SockOptionValue<jewels::networking::SockOption::ip_multicast_if>{.interface_address = "127.0.0.1"},
    SockOptionValue<jewels::networking::SockOption::ip_multicast_loop>{0});
  REQUIRE(maybe_outgoing);
  auto& outgoing_socket = *maybe_outgoing;

  auto maybe_bidir = BidirectionalUdp<Tachyon<io::VarPacket<sizeof(uint32_t)>>>::try_make(
    memres,
    {},
    {},
    {.host = multicast_group, .port = 0U},
    {.host = multicast_group, .port = port},
    SockOptionValue<jewels::networking::SockOption::ip_add_membership>{
      .group_address = {multicast_group}, .local_address = "127.0.0.1"},
    SockOptionValue<jewels::networking::SockOption::ip_multicast_if>{.interface_address = "127.0.0.1"},
    SockOptionValue<jewels::networking::SockOption::ip_multicast_loop>{0});
  REQUIRE(maybe_bidir);
  auto& bidir_socket = *maybe_bidir;
  const auto bidir_assigned_addr = support::get_assigned_addr(bidir_socket->fd());
  REQUIRE(bidir_assigned_addr);
  const uint16_t bidir_port{support::get_port(*bidir_assigned_addr)};

  auto dst_addr = jewels::networking::SocketAddress::create(std::string{multicast_group}, port);
  REQUIRE(dst_addr);
  std::array<char, 4> expected_buffer{'b', 'e', 'e', 'f'};
  REQUIRE(
    ::sendto(
      outgoing_socket->fd(),
      expected_buffer.data(),
      expected_buffer.size(),
      0,
      dst_addr->mutable_ptr(),
      jewels::networking::SocketAddress::byte_size()) == expected_buffer.size());

  std::array<char, 4> recv_buffer{};
  REQUIRE(support::wait_for_readable(incoming_socket->fd()));
  REQUIRE(::recv(incoming_socket->fd(), recv_buffer.data(), recv_buffer.size(), 0) == recv_buffer.size());
  REQUIRE(recv_buffer == expected_buffer);

  REQUIRE(
    ::sendto(
      bidir_socket->fd(),
      expected_buffer.data(),
      expected_buffer.size(),
      0,
      dst_addr->mutable_ptr(),
      jewels::networking::SocketAddress::byte_size()) == expected_buffer.size());

  recv_buffer.fill('\0');
  ::sockaddr_in sender{};
  ::socklen_t sender_size{sizeof(::sockaddr_in)};
  REQUIRE(support::wait_for_readable(incoming_socket->fd()));
  REQUIRE(
    ::recvfrom(
      incoming_socket->fd(),
      recv_buffer.data(),
      recv_buffer.size(),
      0,
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) Needed for socket API
      reinterpret_cast<::sockaddr*>(&sender),
      &sender_size) == recv_buffer.size());
  REQUIRE(recv_buffer == expected_buffer);
  // Even though the bidirectional socket is bound to the multicast address, it
  // should still send from the local address.
  REQUIRE(sender.sin_addr.s_addr == ::htobe32(0x7f000001));
  REQUIRE(sender.sin_port == ::htobe16(bidir_port));
}

} // namespace clockwork::pinion
