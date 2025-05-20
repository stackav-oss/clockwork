// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/io/var_packet.hh"
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/bidirectional_udp.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/detail/socket_payload.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/in_memory_channel.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/sock_opt.hh"
#include "clockwork/pinion/subscriber_handle.hh"
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
#include "jewels/networking/socket_address.hh"
#include "jewels/std/expected.hh"
#include "jewels/std/span.hh"

#include <arpa/inet.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <memory>
#include <memory_resource>
#include <netinet/in.h>
#include <ranges>
#include <span>
#include <string>
#include <utility>

namespace clockwork::pinion
{
TEST_CASE("BidirectionalUdp", "Both directions")
{
  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  using Msg = Tachyon<JustAUInt32>;
  constexpr auto num_slots{10UL};

  const std::pmr::string host = "127.0.0.1";

  auto server = support::Receiver::try_make(std::string{host}, uint16_t{0U});
  REQUIRE(server);
  const auto remote_port = server->port();
  REQUIRE(remote_port);

  auto maybe_bidir =
    BidirectionalUdp<Msg>::try_make(memres, {.host = host, .port = 0U}, {.host = host, .port = *remote_port});
  REQUIRE(maybe_bidir);

  auto bd_socket = *maybe_bidir;
  auto local_addr = support::get_assigned_addr(bd_socket->fd());
  REQUIRE(local_addr);
  auto local_port = ::ntohs(local_addr->sin_port);

  InMemoryChannel<Msg, num_slots> incoming_channel{memres};
  auto incoming_publisher = incoming_channel.make_publisher(1UL);
  auto incoming_subscriber = incoming_channel.make_subscriber();
  REQUIRE(bd_socket->connect_publisher(std::move(incoming_publisher)));
  InMemoryChannel<Msg, num_slots> outgoing_channel{memres};
  auto outgoing_publisher = outgoing_channel.make_publisher(0UL);
  auto outgoing_subscriber = outgoing_channel.make_subscriber();
  //  The socket "subscribes" to this channel to send data
  REQUIRE(bd_socket->connect_subscriber(outgoing_subscriber));

  constexpr uint32_t ref_test_value = 123U;

  // Test that the value published to outgoing channel reaches server
  REQUIRE(support::publish_to<Msg>(outgoing_publisher, ref_test_value));
  bd_socket->write();
  const auto payload = server->read<sizeof(uint32_t)>();
  REQUIRE(payload);
  REQUIRE(jewels::memory::bit_cast_to<uint32_t>(std::span{*payload}) == ref_test_value);

  // Test that the server can send value to the socket and the value is output to the incoming channel
  auto client_addr = jewels::networking::SocketAddress::create(std::string{host}, local_port);
  REQUIRE(
    support::send_to(
      std::as_bytes(jewels::as_single_item_span(ref_test_value)),
      server->fd(),
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) needed for socket API
      *(reinterpret_cast<const sockaddr_in*>(client_addr->ptr()))) == sizeof(ref_test_value));
  bd_socket->read();
  const pinion::BufferIterator next_to_consume{};
  auto available = pinion::available_starting_from(incoming_subscriber.available(), next_to_consume);
  REQUIRE(available);
  REQUIRE(available->size() == 1UL);
  auto msg_range = pinion::to_message_range<const Msg>(*available);
  REQUIRE(msg_range);
  auto msg = *std::begin(*msg_range);
  REQUIRE(std::ranges::equal(msg_as_byte_span(msg), std::as_bytes(jewels::as_single_item_span(ref_test_value))));
}

TEST_CASE("Socket options")
{
  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  const SockOptionValue<jewels::networking::SockOption::so_reuse_address> value{GENERATE(0, 1)};
  auto maybe_udp = BidirectionalUdp<Tachyon<io::VarPacket<sizeof(uint32_t)>>>::try_make(
    memres,
    {.host = std::pmr::string{"127.0.0.1"}, .port = uint16_t{0U}},
    {.host = std::pmr::string{"127.0.0.1"}, .port = uint16_t{0U}},
    value);
  REQUIRE(maybe_udp);
  auto& udp = *maybe_udp;
  REQUIRE(jewels::networking::get_sock_opt<jewels::networking::SockOption::so_reuse_address>(udp->fd()) == value.value);
}

} // namespace clockwork::pinion
