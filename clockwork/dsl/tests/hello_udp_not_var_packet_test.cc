// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/dsl/tests/support/hello_udp_not_var_packet.hh"
#include "clockwork/pinion/incoming_udp.hh"
#include "clockwork/pinion/outgoing_udp.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>
#include <gsl/util>

#include <memory_resource>
#include <string>
#include <string_view>
#include <type_traits>

namespace clockwork::testing
{

TEST_CASE("UDP Socket")
{
  SECTION("Incoming")
  {
    auto expected_uuid = jewels::Uuid<common::IoConnectionClassId>::from_string("b93bf8ef-0edb-5438-b35b-ebb21e31b14b");
    REQUIRE(expected_uuid);
    REQUIRE(IncomingUdpSocket::uuid == *expected_uuid);
    REQUIRE(IncomingUdpSocket::host == "127.0.0.1");
    REQUIRE(IncomingUdpSocket::port == 0U);
    const jewels::memory::MemoryResource memres(std::pmr::new_delete_resource());
    auto maybe_socket = IncomingUdpSocket::try_make(memres);
    STATIC_REQUIRE(
      std::is_same_v<
        decltype(maybe_socket)::value_type,
        jewels::memory::NonNullSharedPtr<pinion::IncomingUdp<Tachyon<NotVarPacket>>>>);
    REQUIRE(maybe_socket);
  }
  SECTION("Outgoing")
  {
    auto expected_uuid = jewels::Uuid<common::IoConnectionClassId>::from_string("d5878136-012a-569d-b7d0-0c580620a745");
    REQUIRE(expected_uuid);
    REQUIRE(OutgoingUdpSocket::uuid == *expected_uuid);
    REQUIRE(OutgoingUdpSocket::host == "127.0.0.1");
    REQUIRE(OutgoingUdpSocket::port == 0U);
    const jewels::memory::MemoryResource memres(std::pmr::new_delete_resource());
    auto maybe_socket = OutgoingUdpSocket::try_make(memres);
    STATIC_REQUIRE(
      std::is_same_v<
        decltype(maybe_socket)::value_type,
        jewels::memory::NonNullSharedPtr<pinion::OutgoingUdp<Tachyon<NotVarPacket>>>>);
    REQUIRE(maybe_socket);
  }
}

} // namespace clockwork::testing
