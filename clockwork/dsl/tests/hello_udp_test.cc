// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/process_description.hh"
#include "clockwork/dsl/tests/support/hello_udp.hh"
#include "clockwork/io/var_packet.hh"
#include "clockwork/pinion/bidirectional_udp.hh"
#include "clockwork/pinion/incoming_udp.hh"
#include "clockwork/pinion/outgoing_udp.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/networking/sock_opt.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>
#include <endian.h>
#include <gsl/util>

#include <memory>
#include <memory_resource>
#include <netinet/in.h>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <type_traits>

namespace clockwork::testing
{

TEST_CASE("UDP Socket")
{
  SECTION("Incoming")
  {
    auto expected_uuid = jewels::Uuid<common::IoConnectionClassId>::from_string("65eef3b5-0737-5208-a6dd-5fd54443cc73");
    REQUIRE(expected_uuid);
    REQUIRE(IncomingUdpSocket::uuid == *expected_uuid);
    REQUIRE(IncomingUdpSocket::host == "127.0.0.1");
    REQUIRE(IncomingUdpSocket::port == 0U);
    const jewels::memory::MemoryResource memres(std::pmr::new_delete_resource());
    auto maybe_socket = IncomingUdpSocket::try_make(memres);
    STATIC_REQUIRE(
      std::is_same_v<
        decltype(maybe_socket)::value_type,
        jewels::memory::NonNullSharedPtr<pinion::IncomingUdp<Tachyon<io::VarPacket<4UL>>>>>);
    REQUIRE(maybe_socket);
    auto& socket = *maybe_socket;

    REQUIRE(jewels::networking::get_sock_opt<jewels::networking::SockOption::so_reuse_address>(socket->fd()) == 1);
  }
  SECTION("Outgoing")
  {
    auto expected_uuid = jewels::Uuid<common::IoConnectionClassId>::from_string("f87887d3-5289-5d8b-a272-37dc0fc1ab4b");
    REQUIRE(expected_uuid);
    REQUIRE(OutgoingUdpSocket::uuid == *expected_uuid);
    REQUIRE(OutgoingUdpSocket::host == "127.0.0.1");
    REQUIRE(OutgoingUdpSocket::port == 0U);
    const jewels::memory::MemoryResource memres(std::pmr::new_delete_resource());
    auto maybe_socket = OutgoingUdpSocket::try_make(memres);
    STATIC_REQUIRE(
      std::is_same_v<
        decltype(maybe_socket)::value_type,
        jewels::memory::NonNullSharedPtr<pinion::OutgoingUdp<Tachyon<io::VarPacket<4UL>>>>>);
    REQUIRE(maybe_socket);
  }
  SECTION("Multicast")
  {
    auto expected_uuid_in =
      jewels::Uuid<common::IoConnectionClassId>::from_string("d21f27f5-ce0c-5302-89fa-27a57a3963f5");
    REQUIRE(expected_uuid_in);
    REQUIRE(IncomingMulticast::uuid == *expected_uuid_in);
    REQUIRE(IncomingMulticast::host == "239.22.0.2");
    REQUIRE(IncomingMulticast::port == 5000U);
    const jewels::memory::MemoryResource memres(std::pmr::new_delete_resource());
    auto maybe_incoming = IncomingMulticast::try_make(memres);
    STATIC_REQUIRE(
      std::is_same_v<
        decltype(maybe_incoming)::value_type,
        jewels::memory::NonNullSharedPtr<pinion::IncomingUdp<Tachyon<io::VarPacket<4UL>>>>>);
    REQUIRE(maybe_incoming);
    auto& incoming_socket = *maybe_incoming;

    auto loop_result =
      jewels::networking::get_sock_opt<jewels::networking::SockOption::ip_multicast_loop>(incoming_socket->fd());
    REQUIRE(loop_result);
    REQUIRE(*loop_result == 0);

    auto expected_uuid_out =
      jewels::Uuid<common::IoConnectionClassId>::from_string("f874591f-e35f-5c90-a8d8-ddbd79642cee");
    REQUIRE(expected_uuid_out);
    REQUIRE(OutgoingMulticast::uuid == *expected_uuid_out);
    REQUIRE(OutgoingMulticast::host == "239.22.0.2");
    REQUIRE(OutgoingMulticast::port == 5000U);
    auto maybe_outgoing = OutgoingMulticast::try_make(memres);
    STATIC_REQUIRE(
      std::is_same_v<
        decltype(maybe_outgoing)::value_type,
        jewels::memory::NonNullSharedPtr<pinion::OutgoingUdp<Tachyon<io::VarPacket<4UL>>>>>);
    REQUIRE(maybe_outgoing);
    auto& outgoing_socket = *maybe_outgoing;

    auto multicast_if =
      jewels::networking::get_sock_opt<jewels::networking::SockOption::ip_multicast_if>(outgoing_socket->fd());
    REQUIRE(multicast_if);
    REQUIRE(multicast_if->s_addr == ::be32toh(0x7f000001)); // 127.0.0.1
                                                            //
    auto expected_uuid_bidirectional =
      jewels::Uuid<common::IoConnectionClassId>::from_string("5c225acb-1579-5a79-96c6-a49a6c06aec1");
    REQUIRE(expected_uuid_out);
    REQUIRE(BidirectionalMulticast::uuid == *expected_uuid_bidirectional);
    REQUIRE(BidirectionalMulticast::host == "239.22.0.2");
    REQUIRE(BidirectionalMulticast::port == 5001U);
    REQUIRE(BidirectionalMulticast::remote_port == 5002U);
    REQUIRE(BidirectionalMulticast::remote_host == "239.22.0.2");
    auto maybe_bidirectional = BidirectionalMulticast::try_make(memres);
    STATIC_REQUIRE(
      std::is_same_v<
        decltype(maybe_bidirectional)::value_type,
        jewels::memory::NonNullSharedPtr<pinion::BidirectionalUdp<Tachyon<io::VarPacket<4UL>>>>>);
    REQUIRE(maybe_bidirectional);
    auto& bidirectional_socket = *maybe_bidirectional;

    loop_result =
      jewels::networking::get_sock_opt<jewels::networking::SockOption::ip_multicast_loop>(bidirectional_socket->fd());
    REQUIRE(loop_result);
    REQUIRE(*loop_result == 0);

    multicast_if =
      jewels::networking::get_sock_opt<jewels::networking::SockOption::ip_multicast_if>(bidirectional_socket->fd());
    REQUIRE(multicast_if);
    REQUIRE(multicast_if->s_addr == ::be32toh(0x7f000001)); // 127.0.0.1

    // Make sure we bound to the multicast address.
    ::sockaddr_in addr{};
    ::socklen_t addr_size{sizeof(::sockaddr_in)};
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) Needed for C struct polymorphism
    REQUIRE(::getsockname(bidirectional_socket->fd(), reinterpret_cast<::sockaddr*>(&addr), &addr_size) == 0);
    REQUIRE(addr.sin_addr.s_addr == ::be32toh(0xef160002)); // 239.22.0.2
  }
}

} // namespace clockwork::testing
