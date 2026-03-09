// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/process_description_clk_cc.hh" // IWYU pragma: keep
#include "clockwork/io/network_var_packet_clk_cc.hh"      // IWYU pragma: keep
#include "clockwork/io/var_packet_clk_cc.hh"
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/detail/socket_payload.hh"
#include "clockwork/pinion/in_memory_channel.hh"
#include "clockwork/pinion/io_connection.hh"
#include "clockwork/pinion/outgoing_udp.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/sock_opt.hh"
#include "clockwork/pinion/tests/support/sockets.hh"
#include "clockwork/pinion/tests/support/udp_payloads.hh"
#include "jewels/container/compare.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/memory/bits.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_shared_ptr.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/networking/sock_opt.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <gsl/util>

#include <array>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <span>
#include <string>
#include <type_traits>
#include <utility>

namespace clockwork::pinion
{

TEMPLATE_TEST_CASE("OutgoingUdp", "[VarPacket, Not VarPacket]", io::VarPacket<sizeof(uint32_t)>, JustAUInt32)
{
  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  using Msg = Tachyon<TestType>;
  constexpr auto num_slots{10UL};

  auto receiver = support::Receiver::try_make(std::string{"127.0.0.1"}, uint16_t{0U});
  REQUIRE(receiver);
  const auto assigned_port = receiver->port();
  REQUIRE(assigned_port);

  const auto endpoint_class_id =
    jewels::Uuid<common::EndpointClassId>::from_string("00000000-0000-0000-0000-000000000001");
  REQUIRE(endpoint_class_id);

  auto maybe_outgoing_udp = OutgoingUdp<Msg>::try_make(
    memres, *endpoint_class_id, {.host = std::pmr::string{"127.0.0.1"}, .port = *assigned_port});
  REQUIRE(maybe_outgoing_udp);
  auto outgoing_udp = *std::move(maybe_outgoing_udp);

  SECTION("No subscriber registered yet")
  {
    REQUIRE_THROWS(outgoing_udp->write());
  }

  const auto assigned_addr = support::get_assigned_addr(outgoing_udp->fd());
  REQUIRE(assigned_addr);

  SECTION("Mismatched message sizes")
  {
    auto channel = std::make_unique<InMemoryChannel<bool, num_slots, false>>(memres);
    auto subscriber = channel->make_subscriber();

    // Size of message slot and size of UDP packet are not the same.
    REQUIRE(
      outgoing_udp->connect_subscriber({}, std::move(subscriber)) ==
      jewels::unexpected{IoConnection::Error::invalid_buffer_layout});
  }

  InMemoryChannel<Msg, num_slots, false> channel{memres};

  SECTION("Mismatched endpoint id")
  {
    auto subscriber = channel.make_subscriber();
    REQUIRE(
      outgoing_udp->connect_subscriber({}, std::move(subscriber)) ==
      jewels::unexpected{IoConnection::Error::unexpected_endpoint_id});
  }

  auto publisher = channel.make_publisher(0UL);
  auto subscriber = channel.make_subscriber();
  REQUIRE(outgoing_udp->connect_subscriber(*endpoint_class_id, std::move(subscriber)));

  SECTION("No packets to write ")
  {
    outgoing_udp->write();
  }

  SECTION("Alternating read / write")
  {
    for (auto i = 0U; i < num_slots; ++i)
    {
      REQUIRE(support::publish_to<Msg>(publisher, i));
      outgoing_udp->write();
      const auto payload = receiver->read<sizeof(uint32_t)>();
      REQUIRE(payload);
      REQUIRE(jewels::memory::bit_cast_to<uint32_t>(std::span{*payload}) == i);
    }
  }

  SECTION("Batch read / write")
  {
    for (auto i = 0U; i < num_slots; ++i)
    {
      REQUIRE(support::publish_to<Msg>(publisher, i));
    }
    outgoing_udp->write();
    for (auto i = 0U; i < num_slots; ++i)
    {
      const auto payload = receiver->read<sizeof(uint32_t)>();
      REQUIRE(payload);
      REQUIRE(jewels::memory::bit_cast_to<uint32_t>(std::span{*payload}) == i);
    }
  }

  SECTION("Fell behind")
  {
    {
      // One read/write to initialize the cursor.
      REQUIRE(support::publish_to<Msg>(publisher, 0U));
      outgoing_udp->write();
      const auto payload = receiver->read<sizeof(uint32_t)>();
      REQUIRE(payload);
      REQUIRE(jewels::memory::bit_cast_to<uint32_t>(std::span{*payload}) == 0UL);
    }

    for (auto i = 0U; i < num_slots + 1; ++i)
    {
      REQUIRE(support::publish_to<Msg>(publisher, i));
    }
    outgoing_udp->write();
    for (auto i = 0U; i < num_slots; ++i)
    {
      const auto payload = receiver->read<sizeof(uint32_t)>();
      REQUIRE(payload);
      REQUIRE(jewels::memory::bit_cast_to<uint32_t>(std::span{*payload}) == i + 1UL);
    }
  }

  SECTION("Payload smaller than capacity")
  {
    if constexpr (std::is_same_v<TestType, io::VarPacket<sizeof(uint32_t)>>)
    {
      REQUIRE(support::publish_to<Msg>(publisher, uint16_t{1234}));
      outgoing_udp->write();
      const auto payload = receiver->read<sizeof(uint16_t)>();
      // This will fail if more bytes were written as we read with MSG_TRUNC.
      REQUIRE(payload);
      REQUIRE(jewels::memory::bit_cast_to<uint16_t>(std::span{*payload}) == uint16_t{1234U});
    }
  }
}

TEST_CASE("Invalid host")
{
  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  REQUIRE_FALSE(
    OutgoingUdp<Tachyon<io::VarPacket<sizeof(uint32_t)>>>::try_make(
      memres, {}, {.host = std::pmr::string{"127.0.0.999"}, .port = uint16_t{0U}}));
}

TEST_CASE("Socket options")
{
  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  const SockOptionValue<jewels::networking::SockOption::so_reuse_address> value{GENERATE(0, 1)};
  auto maybe_udp = OutgoingUdp<Tachyon<io::VarPacket<sizeof(uint32_t)>>>::try_make(
    memres, {}, {.host = std::pmr::string{"127.0.0.1"}, .port = uint16_t{0U}}, value);
  REQUIRE(maybe_udp);
  auto& udp = *maybe_udp;
  REQUIRE(jewels::networking::get_sock_opt<jewels::networking::SockOption::so_reuse_address>(udp->fd()) == value.value);
}

} // namespace clockwork::pinion
