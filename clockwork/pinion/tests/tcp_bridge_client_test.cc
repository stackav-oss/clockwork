// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/buffer_layout.hh"
#include "clockwork/pinion/detail/socket_common.hh"
#include "clockwork/pinion/detail/tcp_socket.hh"
#include "clockwork/pinion/in_memory_channel.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/slot_ref.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "clockwork/pinion/tcp_bridge_client.hh"
#include "clockwork/pinion/tcp_bridge_common.hh"
#include "clockwork/pinion/tcp_bridge_config_clk_cc.hh"
#include "clockwork/pinion/tests/support/bridge_test_support.hh"
#include "clockwork/pinion/tests/support/pub_sub.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/container/compare.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/networking/sock_opt.hh"
#include "jewels/networking/socket_address.hh"
#include "jewels/std/span.hh"
#include "jewels/testing/fix_catch2_cerr_nonthreadsafe_redirect.hh" // IWYU pragma: keep
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <arpa/inet.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <netinet/in.h>
#include <ranges>
#include <span>
#include <string_view>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace clockwork::pinion
{

using jewels::Out;

TEST_CASE("TcpBridgeClient | Receive")
{
  const auto is_bulk_data = GENERATE(false, true);
  CAPTURE(is_bulk_data);
  using Msg = uint32_t;
  constexpr size_t num_slots = 3;
  constexpr size_t max_observer = 1;

  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  auto channel = std::make_shared<InMemoryChannel<Msg, num_slots, false>>(memres);
  auto [listen_socket, listen_addr] = support::make_listen_socket();

  auto config = std::make_unique<Tappy<TcpBridgeClientConfig>>();
  config->get_mutable_publisher_endpoint().set_publisher_id(jewels::Uuid<common::EndpointInstanceId>::random_uuid());
  config->get_mutable_publisher_endpoint().get_underlying_channel_name().set_truncate("test_channel");
  config->get_mutable_publisher_endpoint().set_is_bulk_data(is_bulk_data);
  config->get_underlying_server_address().set_truncate(support::local_socket_host);
  config->set_server_port(::ntohs(listen_addr.port()));

  const auto diagnostics_state = std::make_shared<TcpBridgeDiagnosticsState>();

  // Create the bridge.
  const auto bridge_client =
    TcpBridgeClient::make(memres, *config, channel->make_publisher(max_observer), channel, diagnostics_state);
  REQUIRE(bridge_client);
  config.reset();

  const auto accepted = support::accept_connection(*listen_socket);
  REQUIRE(accepted >= 0);
  REQUIRE(set_nonblocking(accepted, true));
  REQUIRE(jewels::networking::set_sock_opt<jewels::networking::SockOption::tcp_nodelay>(accepted, 1));

  SECTION("Simple Receive and Publish")
  {
    const std::vector<Msg> expected{1111U, 2222U, 3333U};
    const std::vector<int64_t> expected_publish_stamps{4444L, 5555L, 6666L};
    const std::vector<int64_t> expected_commit_stamps{5555L, 6666L, 7777L};
    const std::vector<uint64_t> expected_seqnos{444UL, 555UL, 666UL};
    for (size_t i = 0; i < expected.size(); ++i)
    {
      support::send_payload(
        accepted,
        expected_seqnos[i],
        expected_publish_stamps[i],
        expected_commit_stamps[i],
        std::as_bytes(jewels::as_single_item_span(expected[i])));
      CHECK(support::recv_acknowledgement(accepted, expected_seqnos[i]));
    }
    CHECK(testing::dump<Msg>(channel) == expected);
    auto messages = channel->available();
    for (size_t i = 0; i < messages.size(); ++i)
    {
      CHECK(messages[static_cast<int64_t>(i)].header()->publish_timestamp == expected_publish_stamps[i]);
      CHECK(messages[static_cast<int64_t>(i)].header()->sequence_number == expected_seqnos[i]);
      CHECK(messages[static_cast<int64_t>(i)].header()->source_commit_timestamp == expected_commit_stamps[i]);
    }
    auto diagnostics_counters = diagnostics_state->get_and_reset_counters();
    CHECK(support::compare_diagnostics_counters(diagnostics_counters, TcpBridgeDiagnosticsCounters{}));
  }

  SECTION("Incremental Receive")
  {
    constexpr size_t chunk_size = 2U;
    std::vector<Msg> expected{0xdeadbeefU, 0xc000ffee, 0xbaadf00d};
    std::vector<int64_t> expected_publish_stamps{1234L, 5678L, 9012L};
    std::vector<int64_t> expected_commit_stamps{2345L, 6789L, 1234L};
    std::vector<uint64_t> expected_seqnos{22U, 23U, 24U};
    auto expected_span = std::span(expected.data(), expected.size());
    for (size_t i = 0; i < expected.size(); i++)
    {
      CAPTURE(i);
      TcpMessageTail tail{};
      std::vector<std::byte> compressed_message;
      support::compress_message(
        Out{compressed_message},
        Out{tail.counts_checksum},
        Out{tail.data_checksum},
        std::as_bytes(expected_span.subspan(i, 1)));
      const auto msg_span = std::span{compressed_message.data(), compressed_message.size()};
      // First, send over just the header.
      support::send_header(
        accepted, expected_seqnos[i], msg_span.size(), expected_publish_stamps[i], expected_commit_stamps[i]);
      CHECK(channel->available().size() == i);

      // Pretend the message got delivered in two chunks for some reason.
      REQUIRE(static_cast<size_t>(::send(accepted, msg_span.first(chunk_size).data(), chunk_size, 0)) == chunk_size);
      CHECK(channel->available().size() == i);
      REQUIRE(chunk_size == 2U);
      REQUIRE(
        static_cast<size_t>(::send(accepted, msg_span.subspan(chunk_size).data(), msg_span.size() - chunk_size, 0)) ==
        msg_span.size() - chunk_size);
      CHECK(channel->available().size() == i);

      // Send the tail.
      REQUIRE(::send(accepted, &tail, sizeof(tail), 0) == sizeof(tail));
      CHECK(support::recv_acknowledgement(accepted, expected_seqnos[i]));
      CHECK(channel->available().size() == i + 1);
    }
    CHECK(testing::dump<Msg>(channel) == expected);
    auto messages = channel->available();
    for (size_t i = 0; i < messages.size(); ++i)
    {
      CHECK(messages[static_cast<int64_t>(i)].header()->publish_timestamp == expected_publish_stamps[i]);
      CHECK(messages[static_cast<int64_t>(i)].header()->sequence_number == expected_seqnos[i]);
      CHECK(messages[static_cast<int64_t>(i)].header()->source_commit_timestamp == expected_commit_stamps[i]);
    }
    auto diagnostics_counters = diagnostics_state->get_and_reset_counters();
    CHECK(support::compare_diagnostics_counters(diagnostics_counters, TcpBridgeDiagnosticsCounters{}));
  }

  SECTION("Invalid Message - bad compression counts failure")
  {
    const Msg msg{0xbadU};
    support::send_corrupted_payload(
      accepted, 0, 0L, 0L, std::as_bytes(jewels::as_single_item_span(msg)), support::CorruptionType::corrupt_counts);
    // The client should have dropped the message but still sent an acknowledgement
    CHECK(support::recv_acknowledgement(accepted, 0U));
    CHECK(channel->available().empty());
    auto diagnostics_counters = diagnostics_state->get_and_reset_counters();
    CHECK(
      support::compare_diagnostics_counters(
        diagnostics_counters, TcpBridgeDiagnosticsCounters{.malformed_messages = 1}));
  }

  SECTION("Invalid Message - bad compression data failure")
  {
    const Msg msg{0xbadU};
    support::send_corrupted_payload(
      accepted, 0, 0L, 0L, std::as_bytes(jewels::as_single_item_span(msg)), support::CorruptionType::corrupt_data);
    // The client should have dropped the message but still sent an acknowledgement
    CHECK(support::recv_acknowledgement(accepted, 0U));
    CHECK(channel->available().empty());
    auto diagnostics_counters = diagnostics_state->get_and_reset_counters();
    CHECK(
      support::compare_diagnostics_counters(
        diagnostics_counters, TcpBridgeDiagnosticsCounters{.malformed_messages = 1}));
  }


  SECTION("Invalid Message - invalid message header checksum")
  {
    const Msg msg{0xbadU};
    support::send_corrupted_payload(
      accepted,
      0,
      0L,
      0L,
      std::as_bytes(jewels::as_single_item_span(msg)),
      support::CorruptionType::corrupt_header_checksum);
    // The client should have detected the bad checksum and closed the connection
    CHECK(support::socket_is_closed(accepted));
    REQUIRE(channel->available().empty());
    auto diagnostics_counters = diagnostics_state->get_and_reset_counters();
    diagnostics_counters.max_bridge_latency = {};
    diagnostics_counters.max_latency_channel_name = {};
    CHECK(
      support::compare_diagnostics_counters(
        diagnostics_counters, TcpBridgeDiagnosticsCounters{.closed_socket_count = 1, .malformed_messages = 1}));
  }

  SECTION("Invalid Message - invalid message size")
  {
    const Msg msg{0xbadU};
    support::send_corrupted_payload(
      accepted, 0, 0L, 0L, std::as_bytes(jewels::as_single_item_span(msg)), support::CorruptionType::corrupt_size);
    // The client should have detected the message size mismatch, discarded any
    // pending writes to the channel, and closed the socket.
    CHECK(support::socket_is_closed(accepted));
    CHECK(channel->available().empty());
    auto diagnostics_counters = diagnostics_state->get_and_reset_counters();
    CHECK(
      support::compare_diagnostics_counters(
        diagnostics_counters, TcpBridgeDiagnosticsCounters{.closed_socket_count = 1, .malformed_messages = 1}));
  }

  SECTION("Invalid Message - invalid payload type")
  {
    const Msg msg{0xbadU};
    support::send_corrupted_payload(
      accepted,
      0,
      0L,
      0L,
      std::as_bytes(jewels::as_single_item_span(msg)),
      support::CorruptionType::corrupt_payload_type);
    // The client should have detected the bad payload type and closed the connection
    CHECK(support::socket_is_closed(accepted));
    CHECK(channel->available().empty());
    auto diagnostics_counters = diagnostics_state->get_and_reset_counters();
    CHECK(
      support::compare_diagnostics_counters(
        diagnostics_counters, TcpBridgeDiagnosticsCounters{.closed_socket_count = 1, .malformed_messages = 1}));
  }

  SECTION("Invalid Message - invalid magic number")
  {
    const Msg msg{0xbadU};
    support::send_corrupted_payload(
      accepted,
      0,
      0L,
      0L,
      std::as_bytes(jewels::as_single_item_span(msg)),
      support::CorruptionType::corrupt_magic_number);
    // The client should have detected the bad magic number and closed the connection
    CHECK(support::socket_is_closed(accepted));
    CHECK(channel->available().empty());
    auto diagnostics_counters = diagnostics_state->get_and_reset_counters();
    CHECK(
      support::compare_diagnostics_counters(
        diagnostics_counters, TcpBridgeDiagnosticsCounters{.closed_socket_count = 1, .malformed_messages = 1}));
  }

  bridge_client->request_stop();
  ::close(accepted);
}

TEST_CASE("TcpBridgeClient | Reconnect")
{
  const auto is_bulk_data = GENERATE(false, true);
  CAPTURE(is_bulk_data);
  using Msg = uint32_t;
  constexpr size_t num_slots = 8;
  constexpr size_t max_observer = 1;

  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  auto channel = std::make_shared<InMemoryChannel<Msg, num_slots, false>>(memres);
  auto [listen_socket, listen_addr] = support::make_listen_socket();

  auto config = std::make_unique<Tappy<TcpBridgeClientConfig>>();
  config->get_mutable_publisher_endpoint().set_publisher_id(jewels::Uuid<common::EndpointInstanceId>::random_uuid());
  config->get_mutable_publisher_endpoint().get_underlying_channel_name().set_truncate("test_channel");
  config->get_mutable_publisher_endpoint().set_is_bulk_data(is_bulk_data);
  config->get_underlying_server_address().set_truncate(support::local_socket_host);
  config->set_server_port(::ntohs(listen_addr.port()));

  auto diagnostics_state = std::make_shared<TcpBridgeDiagnosticsState>();

  // Create the bridge.
  auto bridge_client =
    TcpBridgeClient::make(memres, *config, channel->make_publisher(max_observer), channel, diagnostics_state);
  REQUIRE(bridge_client);
  config.reset();

  auto accepted = support::accept_connection(*listen_socket);
  REQUIRE(accepted >= 0);
  REQUIRE(set_nonblocking(accepted, true));
  REQUIRE(jewels::networking::set_sock_opt<jewels::networking::SockOption::tcp_nodelay>(accepted, 1));

  std::vector<Msg> expected1{1111U, 2222U, 3333U};
  std::vector<int64_t> expected_publish_stamps1{4444L, 5555L, 6666L};
  std::vector<int64_t> expected_commit_stamps1{5555L, 6666L, 7777L};
  std::vector<uint64_t> expected_seqnos1{4UL, 5UL, 6UL};
  for (size_t i = 0; i < expected1.size(); ++i)
  {
    support::send_payload(
      accepted,
      expected_seqnos1[i],
      expected_publish_stamps1[i],
      expected_commit_stamps1[i],
      std::as_bytes(jewels::as_single_item_span(expected1[i])));
    CHECK(support::recv_acknowledgement(accepted, expected_seqnos1[i]));
  }
  CHECK(testing::dump<Msg>(channel) == expected1);
  auto messages = channel->available();
  for (size_t i = 0; i < messages.size(); ++i)
  {
    CHECK(messages[static_cast<int64_t>(i)].header()->publish_timestamp == expected_publish_stamps1[i]);
    CHECK(messages[static_cast<int64_t>(i)].header()->sequence_number == expected_seqnos1[i]);
    CHECK(messages[static_cast<int64_t>(i)].header()->source_commit_timestamp == expected_commit_stamps1[i]);
  }

  listen_socket.reset();
  ::close(accepted);

  listen_socket = support::make_listen_socket(listen_addr);

  const auto open_deadline = jewels::time::SyncClock::now() + support::default_recv_timeout;
  while (jewels::time::SyncClock::now() < open_deadline)
  {
    accepted = support::accept_connection(*listen_socket);
    if (accepted >= 0)
    {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  while (jewels::time::SyncClock::now() < open_deadline && !bridge_client->is_connected())
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  REQUIRE(bridge_client->is_connected());

  REQUIRE(accepted >= 0);
  REQUIRE(set_nonblocking(accepted, true));
  REQUIRE(jewels::networking::set_sock_opt<jewels::networking::SockOption::tcp_nodelay>(accepted, 1));

  std::vector<Msg> expected2{1111U, 2222U, 3333U, 4444U};
  std::vector<int64_t> expected_publish_stamps2{4444L, 5555L, 6666L, 7777U};
  std::vector<int64_t> expected_commit_stamps2{5555L, 6666L, 7777L, 8888L};
  std::vector<uint64_t> expected_seqnos2{4UL, 5UL, 6UL, 7L};
  for (size_t i = 0; i < expected2.size(); ++i)
  {
    CAPTURE(i);
    support::send_payload(
      accepted,
      expected_seqnos2[i],
      expected_publish_stamps2[i],
      expected_commit_stamps2[i],
      std::as_bytes(jewels::as_single_item_span(expected2[i])));
    CHECK(support::recv_acknowledgement(accepted, expected_seqnos2[i]));
  }
  CHECK(testing::dump<Msg>(channel) == expected2);
  messages = channel->available();
  for (size_t i = 0; i < messages.size(); ++i)
  {
    CHECK(messages[static_cast<int64_t>(i)].header()->publish_timestamp == expected_publish_stamps2[i]);
    CHECK(messages[static_cast<int64_t>(i)].header()->sequence_number == expected_seqnos2[i]);
    CHECK(messages[static_cast<int64_t>(i)].header()->source_commit_timestamp == expected_commit_stamps2[i]);
  }

  auto diagnostics_counters = diagnostics_state->get_and_reset_counters();
  CHECK(
    support::compare_diagnostics_counters(
      diagnostics_counters, TcpBridgeDiagnosticsCounters{.closed_socket_count = 1}));

  bridge_client->request_stop();
  ::close(accepted);
}

TEST_CASE("TcpBridgeClient | Reconnect after no keep-alives")
{
  const auto is_bulk_data = GENERATE(false, true);
  CAPTURE(is_bulk_data);
  using Msg = uint32_t;
  constexpr size_t num_slots = 8;
  constexpr size_t max_observer = 1;

  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  auto channel = std::make_shared<InMemoryChannel<Msg, num_slots, false>>(memres);
  auto [listen_socket, listen_addr] = support::make_listen_socket();

  auto config = std::make_unique<Tappy<TcpBridgeClientConfig>>();
  config->get_mutable_publisher_endpoint().set_publisher_id(jewels::Uuid<common::EndpointInstanceId>::random_uuid());
  config->get_mutable_publisher_endpoint().get_underlying_channel_name().set_truncate("test_channel");
  config->get_mutable_publisher_endpoint().set_is_bulk_data(is_bulk_data);
  config->get_underlying_server_address().set_truncate(support::local_socket_host);
  config->set_server_port(::ntohs(listen_addr.port()));

  auto diagnostics_state = std::make_shared<TcpBridgeDiagnosticsState>();

  // Create the bridge.
  auto bridge_client =
    TcpBridgeClient::make(memres, *config, channel->make_publisher(max_observer), channel, diagnostics_state);
  REQUIRE(bridge_client);
  config.reset();

  auto accepted = support::accept_connection(*listen_socket);
  REQUIRE(accepted >= 0);
  REQUIRE(set_nonblocking(accepted, true));
  REQUIRE(jewels::networking::set_sock_opt<jewels::networking::SockOption::tcp_nodelay>(accepted, 1));

  std::vector<Msg> expected1{1111U, 2222U, 3333U};
  std::vector<int64_t> expected_publish_stamps1{4444L, 5555L, 6666L};
  std::vector<int64_t> expected_commit_stamps1{5555L, 6666L, 7777L};
  std::vector<uint64_t> expected_seqnos1{4UL, 5UL, 6UL};
  for (size_t i = 0; i < expected1.size(); ++i)
  {
    support::send_payload(
      accepted,
      expected_seqnos1[i],
      expected_publish_stamps1[i],
      expected_commit_stamps1[i],
      std::as_bytes(jewels::as_single_item_span(expected1[i])));
    CHECK(support::recv_acknowledgement(accepted, expected_seqnos1[i]));
  }
  CHECK(testing::dump<Msg>(channel) == expected1);
  auto messages = channel->available();
  for (size_t i = 0; i < messages.size(); ++i)
  {
    CHECK(messages[static_cast<int64_t>(i)].header()->publish_timestamp == expected_publish_stamps1[i]);
    CHECK(messages[static_cast<int64_t>(i)].header()->sequence_number == expected_seqnos1[i]);
    CHECK(messages[static_cast<int64_t>(i)].header()->source_commit_timestamp == expected_commit_stamps1[i]);
  }

  CHECK(support::socket_is_closed(accepted, tcp_bridge_reconnect_interval + std::chrono::seconds(2)));
  ::close(accepted);

  const auto open_deadline = jewels::time::SyncClock::now() + support::default_recv_timeout;
  while (jewels::time::SyncClock::now() < open_deadline)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    accepted = support::accept_connection(*listen_socket);
    if (accepted >= 0)
    {
      break;
    }
  }
  while (!bridge_client->is_connected() && jewels::time::SyncClock::now() < open_deadline)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  REQUIRE(bridge_client->is_connected());

  REQUIRE(accepted >= 0);
  REQUIRE(set_nonblocking(accepted, true));
  REQUIRE(jewels::networking::set_sock_opt<jewels::networking::SockOption::tcp_nodelay>(accepted, 1));

  std::vector<Msg> expected2{1111U, 2222U, 3333U, 4444U};
  std::vector<int64_t> expected_publish_stamps2{4444L, 5555L, 6666L, 7777U};
  std::vector<int64_t> expected_commit_stamps2{5555L, 6666L, 7777L, 8888L};
  std::vector<uint64_t> expected_seqnos2{4UL, 5UL, 6UL, 7L};
  for (size_t i = 0; i < expected2.size(); ++i)
  {
    support::send_payload(
      accepted,
      expected_seqnos2[i],
      expected_publish_stamps2[i],
      expected_commit_stamps2[i],
      std::as_bytes(jewels::as_single_item_span(expected2[i])));
    CHECK(support::recv_acknowledgement(accepted, expected_seqnos2[i]));
  }
  CHECK(testing::dump<Msg>(channel) == expected2);
  messages = channel->available();
  for (size_t i = 0; i < messages.size(); ++i)
  {
    CHECK(messages[static_cast<int64_t>(i)].header()->publish_timestamp == expected_publish_stamps2[i]);
    CHECK(messages[static_cast<int64_t>(i)].header()->sequence_number == expected_seqnos2[i]);
    CHECK(messages[static_cast<int64_t>(i)].header()->source_commit_timestamp == expected_commit_stamps2[i]);
  }

  bridge_client->request_stop();
  ::close(accepted);
}

} // namespace clockwork::pinion
