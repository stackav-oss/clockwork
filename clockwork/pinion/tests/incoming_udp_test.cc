// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/process_description.hh" // IWYU pragma: keep
#include "clockwork/io/network_var_packet.hh"
#include "clockwork/io/var_packet.hh"
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/detail/socket_payload.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/in_memory_channel.hh"
#include "clockwork/pinion/incoming_udp.hh"
#include "clockwork/pinion/io_connection.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/sock_opt.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "clockwork/pinion/tests/support/sockets.hh"
#include "clockwork/pinion/tests/support/udp_payloads.hh"
#include "jewels/container/compare.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_shared_ptr.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/networking/sock_opt.hh"
#include "jewels/networking/socket_address.hh"
#include "jewels/std/expected.hh"
#include "jewels/std/span.hh"
#include "jewels/uuid/uuid.hh"

#include <arpa/inet.h>
#include <boost/iterator/iterator_facade.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <gsl/util>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <memory_resource>
#include <netinet/in.h>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <thread>
#include <utility>

namespace clockwork::pinion
{

TEMPLATE_TEST_CASE(
  "IncomingUdp",
  "[NetworkVarPacket, VarPacket, Not VarPacket]",
  io::NetworkVarPacket<sizeof(uint32_t)>,
  io::VarPacket<sizeof(uint32_t)>,
  JustAUInt32)
{
  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  using Msg = Tachyon<TestType>;
  constexpr auto num_slots{10UL};

  const support::Sender sender{};
  constexpr auto address{"127.0.0.1"};
  constexpr auto incoming_port{0U};
  constexpr auto batch_size{1UL};
  const auto endpoint_class_id =
    jewels::Uuid<common::EndpointClassId>::from_string("00000000-0000-0000-0000-000000000001");
  REQUIRE(endpoint_class_id);
  auto maybe_incoming_udp = IncomingUdp<Msg>::try_make(
    memres, *endpoint_class_id, {.host = std::pmr::string{address}, .port = uint16_t{incoming_port}}, batch_size);
  REQUIRE(maybe_incoming_udp);
  auto incoming_udp = *std::move(maybe_incoming_udp);

  SECTION("No publisher registered yet")
  {
    REQUIRE_THROWS(incoming_udp->read());
  }

  SECTION("Mismatched message sizes")
  {
    auto channel = std::make_unique<InMemoryChannel<bool, num_slots>>(memres);
    auto publisher = channel->make_publisher(0UL);

    // Size of message slot and size of UDP packet are not the same.
    REQUIRE(
      incoming_udp->connect_publisher({}, std::move(publisher)) ==
      jewels::unexpected{IoConnection::Error::invalid_buffer_layout});
  }

  auto channel = std::make_unique<InMemoryChannel<Msg, num_slots>>(memres);
  auto subscriber = channel->make_subscriber();
  SECTION("Mismatched endpoint id")
  {
    auto publisher = channel->make_publisher(0UL);
    REQUIRE(
      incoming_udp->connect_publisher({}, std::move(publisher)) ==
      jewels::unexpected{IoConnection::Error::unexpected_endpoint_id});
  }

  auto publisher = channel->make_publisher(0UL);
  REQUIRE(incoming_udp->connect_publisher(*endpoint_class_id, std::move(publisher)));

  const auto assigned_addr = support::get_assigned_addr(incoming_udp->fd());
  REQUIRE(assigned_addr);

  SECTION("Payload of size zero")
  {
    REQUIRE(sender(std::span<const std::byte>{}, *assigned_addr) == 0UL);
    REQUIRE(support::wait_for_readable(incoming_udp->fd()));
    REQUIRE_THROWS(incoming_udp->read());
  }

  SECTION("Payload larger than capacity")
  {
    REQUIRE(sender(as_bytes(jewels::as_single_item_span(1UL)), *assigned_addr) == 8UL);
    REQUIRE(support::wait_for_readable(incoming_udp->fd()));
    REQUIRE_THROWS(incoming_udp->read());
  }

  SECTION("Payload smaller than capacity")
  {
    // Bind Sender to an address because the NetworkVarPacket validates the source address.
    const auto send_addr = jewels::networking::SocketAddress::create(std::string{address}, 0U);
    REQUIRE(send_addr);
    REQUIRE(::bind(sender.fd(), send_addr->ptr(), jewels::networking::SocketAddress::byte_size()) == 0);
    const auto assigned_send_addr = support::get_assigned_addr(sender.fd());
    REQUIRE(assigned_send_addr);

    const uint16_t payload{1U};
    REQUIRE(sender(as_bytes(jewels::as_single_item_span(payload)), *assigned_addr) == sizeof(payload));
    REQUIRE(support::wait_for_readable(incoming_udp->fd()));

    if constexpr (is_var_packet_v<TestType> || is_network_var_packet_v<TestType>)
    {
      incoming_udp->read();

      auto available = pinion::available_starting_from(subscriber.available(), {});
      REQUIRE(available);
      REQUIRE(available->size() == 1UL);
      auto msg_range = pinion::to_message_range<const Msg>(*available);
      REQUIRE(msg_range);
      auto msg = *std::begin(*msg_range);
      REQUIRE(std::ranges::equal(msg_as_byte_span(msg), as_bytes(jewels::as_single_item_span(payload))));

      // If this is a network_var_packet, ensure that the address and port fields were correctly set.
      if constexpr (is_network_var_packet_v<TestType>)
      {
        REQUIRE(msg.src_address.string_view() == address);
        REQUIRE(msg.src_port == ntohs(assigned_send_addr->sin_port));
        REQUIRE(msg.dst_address.string_view() == address);
        REQUIRE(msg.dst_port == incoming_port);
      }
    }
    else
    {
      REQUIRE_THROWS(incoming_udp->read());
    }
  }

  SECTION("No packets to read")
  {
    REQUIRE(
      support::wait_for_readable(incoming_udp->fd()) ==
      jewels::unexpected{jewels::filesystem::make_error_code(EAGAIN)});
    REQUIRE_THROWS(incoming_udp->read());
  }

  SECTION("Alternating write / read")
  {
    pinion::BufferIterator next_to_consume{};
    for (uint32_t index : std::ranges::views::iota(0U, static_cast<uint32_t>(num_slots)))
    {
      REQUIRE(sender(as_bytes(jewels::as_single_item_span(index)), *assigned_addr) == sizeof(uint32_t));
      REQUIRE(support::wait_for_readable(incoming_udp->fd()));
      incoming_udp->read();

      auto available = pinion::available_starting_from(subscriber.available(), next_to_consume);
      REQUIRE(available);
      REQUIRE(available->size() == 1UL);
      auto msg_range = pinion::to_message_range<const Msg>(*available);
      REQUIRE(msg_range);
      auto msg = *std::begin(*msg_range);
      REQUIRE(std::ranges::equal(msg_as_byte_span(msg), as_bytes(jewels::as_single_item_span(index))));
      next_to_consume = std::next(std::begin(*available));
    }
  }

  SECTION("Bulk write / read")
  {
    for (uint32_t index : std::ranges::views::iota(0U, static_cast<uint32_t>(num_slots)))
    {
      REQUIRE(sender(as_bytes(jewels::as_single_item_span(index)), *assigned_addr) == sizeof(uint32_t));
    }

    pinion::BufferIterator next_to_consume{};
    for (uint32_t index : std::ranges::views::iota(0U, static_cast<uint32_t>(num_slots)))
    {
      REQUIRE(support::wait_for_readable(incoming_udp->fd()));
      incoming_udp->read();

      auto available = pinion::available_starting_from(subscriber.available(), next_to_consume);
      REQUIRE(available);
      REQUIRE(available->size() == 1UL);
      auto msg_range = pinion::to_message_range<const Msg>(*available);
      REQUIRE(msg_range);
      auto msg = *std::begin(*msg_range);
      REQUIRE(std::ranges::equal(msg_as_byte_span(msg), as_bytes(jewels::as_single_item_span(index))));
      next_to_consume = std::next(std::begin(*available));
    }
  }
}

TEMPLATE_TEST_CASE(
  "IncomingUdp batch test",
  "[NetworkVarPacket, VarPacket, Not VarPacket]",
  io::NetworkVarPacket<sizeof(uint32_t)>,
  io::VarPacket<sizeof(uint32_t)>,
  JustAUInt32)
{
  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  using Msg = Tachyon<TestType>;
  constexpr auto num_slots{10UL};

  const support::Sender sender{};
  constexpr auto address{"127.0.0.1"};
  constexpr auto incoming_port{0U};
  constexpr auto batch_size{3UL};
  const auto endpoint_class_id =
    jewels::Uuid<common::EndpointClassId>::from_string("00000000-0000-0000-0000-000000000001");
  REQUIRE(endpoint_class_id);
  auto maybe_incoming_udp = IncomingUdp<Msg>::try_make(
    memres, *endpoint_class_id, {.host = std::pmr::string{address}, .port = uint16_t{incoming_port}}, batch_size);
  REQUIRE(maybe_incoming_udp);
  auto incoming_udp = *std::move(maybe_incoming_udp);

  auto channel = std::make_unique<InMemoryChannel<Msg, num_slots>>(memres);
  auto publisher = channel->make_publisher(0UL);
  auto subscriber = channel->make_subscriber();

  REQUIRE(incoming_udp->connect_publisher(*endpoint_class_id, std::move(publisher)));

  const auto assigned_addr = support::get_assigned_addr(incoming_udp->fd());
  REQUIRE(assigned_addr);

  SECTION("Complete batches")
  {
    constexpr auto num_batches{3UL};
    REQUIRE(num_batches * batch_size < num_slots);
    for (uint32_t index : std::ranges::views::iota(0U, static_cast<uint32_t>(num_batches * batch_size)))
    {
      REQUIRE(sender(as_bytes(jewels::as_single_item_span(index)), *assigned_addr) == sizeof(uint32_t));
    }

    // Unable to rely on support::wait_for_readable because we need
    // multiple messages to be ready.  Using MSG_PEEK with recvmmsg
    // was an attempt to block until ready, but that did not work as
    // expected.  While adding a short yield here does not guarantee
    // the test isn't flaky, it has yet to show a failure with this
    // change.  So it is reliable enough for CI purposes.
    ::std::this_thread::yield();

    pinion::BufferIterator next_to_consume{};
    for (const uint32_t index : std::ranges::views::iota(0U, static_cast<uint32_t>(num_batches)))
    {
      incoming_udp->read();

      auto available = pinion::available_starting_from(subscriber.available(), next_to_consume);
      REQUIRE(available);
      REQUIRE(available->size() == batch_size);
      auto msg_range = pinion::to_message_range<const Msg>(*available);
      REQUIRE(msg_range);
      for (auto msg_index = 0UL; msg_index < batch_size; ++msg_index)
      {
        const auto& msg = (*msg_range)[static_cast<int64_t>(msg_index)];
        REQUIRE(
          std::ranges::equal(
            msg_as_byte_span(msg),
            as_bytes(jewels::as_single_item_span(static_cast<uint32_t>((index * batch_size) + msg_index)))));
      }
      next_to_consume = std::end(*available);
    }
  }

  SECTION("Partial batches")
  {
    REQUIRE(sender(as_bytes(jewels::as_single_item_span(0U)), *assigned_addr) == sizeof(uint32_t));
    REQUIRE(sender(as_bytes(jewels::as_single_item_span(1U)), *assigned_addr) == sizeof(uint32_t));
    REQUIRE(support::wait_for_readable(incoming_udp->fd()));
    incoming_udp->read();
    const pinion::BufferIterator next_to_consume{};
    {
      auto available = pinion::available_starting_from(subscriber.available(), next_to_consume);
      REQUIRE(available);
      REQUIRE(available->empty());
    }
    REQUIRE(sender(as_bytes(jewels::as_single_item_span(2U)), *assigned_addr) == sizeof(uint32_t));
    REQUIRE(support::wait_for_readable(incoming_udp->fd()));
    incoming_udp->read();

    auto available = pinion::available_starting_from(subscriber.available(), next_to_consume);
    REQUIRE(available);
    REQUIRE(available->size() == batch_size);
    auto msg_range = pinion::to_message_range<const Msg>(*available);
    REQUIRE(msg_range);
    for (auto msg_index = 0U; msg_index < batch_size; ++msg_index)
    {
      const auto& msg = (*msg_range)[static_cast<int64_t>(msg_index)];
      REQUIRE(std::ranges::equal(msg_as_byte_span(msg), as_bytes(jewels::as_single_item_span(msg_index))));
    }
  }
}

TEST_CASE("Invalid host")
{
  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  REQUIRE_FALSE(
    IncomingUdp<Tachyon<io::VarPacket<sizeof(uint32_t)>>>::try_make(
      memres, {}, {.host = std::pmr::string{"127.0.0.999"}, .port = uint16_t{0U}}, 1UL));
}

TEST_CASE("Socket options")
{
  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  const SockOptionValue<jewels::networking::SockOption::so_reuse_address> value{GENERATE(0, 1)};
  auto maybe_udp = IncomingUdp<Tachyon<io::VarPacket<sizeof(uint32_t)>>>::try_make(
    memres, {}, {.host = std::pmr::string{"127.0.0.1"}, .port = uint16_t{0U}}, 1UL, value);
  REQUIRE(maybe_udp);
  auto& udp = *maybe_udp;
  REQUIRE(jewels::networking::get_sock_opt<jewels::networking::SockOption::so_reuse_address>(udp->fd()) == value.value);
}

} // namespace clockwork::pinion
