// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/platform_diagnostics_config_clk_cc.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/diagnostics/report_clk_cc.hh"
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/abstract_channel.hh"
#include "clockwork/pinion/abstract_channel_factory.hh"
#include "clockwork/pinion/bridge_status_clk_cc.hh"
#include "clockwork/pinion/buffer_layout.hh"
#include "clockwork/pinion/detail/socket_common.hh"
#include "clockwork/pinion/detail/tcp_socket.hh"
#include "clockwork/pinion/shm_channel_factory.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/slot_ref.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "clockwork/pinion/tcp_bridge.hh"
#include "clockwork/pinion/tcp_bridge_common.hh"
#include "clockwork/pinion/tcp_bridge_config_clk_cc.hh"
#include "clockwork/pinion/tests/support/bridge_test_support.hh"
#include "clockwork/pinion/tests/support/pub_sub.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/container/compare.hh"
#include "jewels/container/tap/var_array.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/networking/sock_opt.hh"
#include "jewels/networking/socket_address.hh"
#include "jewels/std/expected.hh"
#include "jewels/std/span.hh"
#include "jewels/testing/fix_catch2_cerr_nonthreadsafe_redirect.hh" // IWYU pragma: keep
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <arpa/inet.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <netinet/in.h>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <unistd.h>
#include <utility>
#include <vector>

namespace clockwork::pinion
{

namespace
{

using jewels::ok;

/// Test bridge host name
constexpr auto test_host_name = "test_host";

/// Test bridge status channel name
constexpr auto status_channel_name = "status_channel";

} // namespace

TEST_CASE("TcpBridge client")
{
  const auto is_bulk_data = GENERATE(false, true);
  CAPTURE(is_bulk_data);
  using Msg = uint32_t;
  constexpr size_t num_slots = 3U;
  constexpr size_t max_observers = 1U;
  constexpr auto test_channel_name = "test_channel";

  const pinion::BufferLayout test_buffer_layout{
    .num_slots = num_slots,
    .message_size = sizeof(Msg),
    .is_published_once = false,
  };

  const pinion::BufferLayout status_buffer_layout{
    .num_slots = num_slots,
    .message_size = sizeof(Tappy<BridgeStatus>),
    .is_published_once = false,
  };

  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  jewels::testing::TmpDirectoryGuard tmp_dir_guard;
  const auto& test_dir = tmp_dir_guard.get_path().string();
  const auto nmsp = jewels::Uuid<common::EndpointInstanceId>::random_uuid().to_string();

  auto channel_factory_result = ShmChannelFactory::make(memres, nmsp, test_dir);
  REQUIRE(channel_factory_result);
  auto [listen_socket, listen_addr] = support::make_listen_socket();

  // Create the bridge.
  const auto config_ptr = std::make_unique<Tappy<TcpBridgeConfig<>>>();
  auto& config = *config_ptr;
  config.get_underlying_host_name().set_truncate(test_host_name);
  const auto test_channel_uuid = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  auto& client_config = config.get_underlying_bridge_clients().emplace_back();
  client_config.get_mutable_publisher_endpoint().set_publisher_id(test_channel_uuid);
  client_config.get_mutable_publisher_endpoint().get_underlying_channel_name().set_truncate(test_channel_name);
  client_config.get_mutable_publisher_endpoint().set_is_bulk_data(is_bulk_data);
  client_config.get_mutable_publisher_endpoint().get_mutable_buffer_layout().set_num_slots(num_slots);
  client_config.get_mutable_publisher_endpoint().get_mutable_buffer_layout().set_message_size(sizeof(Msg));
  client_config.get_mutable_publisher_endpoint().get_mutable_buffer_layout().set_is_published_once(false);
  client_config.get_underlying_server_address().set_truncate(support::local_socket_host);
  client_config.set_server_port(::ntohs(listen_addr.port()));
  const auto status_channel_uuid = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  auto& status_config = config.get_mutable_status_publish_endpoint();
  status_config.set_publisher_id(status_channel_uuid);
  status_config.get_underlying_channel_name().set_truncate(test_channel_name);
  status_config.get_mutable_buffer_layout().set_num_slots(num_slots);
  status_config.get_mutable_buffer_layout().set_message_size(sizeof(Tappy<BridgeStatus>));
  status_config.get_mutable_buffer_layout().set_is_published_once(false);

  TcpBridge bridge{
    memres,
    std::make_shared<ShmChannelFactory>(*std::move(channel_factory_result)),
    config,
    jewels::time::SyncTime{std::chrono::seconds(1)}};
  REQUIRE(ok(bridge.initialize(config)));

  auto test_subscriber_result = bridge.channel_factory().open_subscriber(
    test_channel_uuid.to_string(), test_channel_name, test_buffer_layout, max_observers);
  REQUIRE(test_subscriber_result);
  auto test_subscriber = test_subscriber_result.value();

  auto status_subscriber_result = bridge.channel_factory().open_subscriber(
    status_channel_uuid.to_string(), status_channel_name, status_buffer_layout, max_observers);
  REQUIRE(status_subscriber_result);
  auto status_subscriber = status_subscriber_result.value();

  const auto accepted = support::accept_connection(*listen_socket);
  REQUIRE(accepted >= 0);
  REQUIRE(set_nonblocking(accepted, true));
  REQUIRE(jewels::networking::set_sock_opt<jewels::networking::SockOption::tcp_nodelay>(accepted, 1));

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
  CHECK(testing::dump<Msg>(test_subscriber) == expected);
  auto test_messages = test_subscriber->available();
  for (size_t i = 0; i < test_messages.size(); ++i)
  {
    CHECK(test_messages[static_cast<int64_t>(i)].header()->publish_timestamp == expected_publish_stamps[i]);
    CHECK(test_messages[static_cast<int64_t>(i)].header()->sequence_number == expected_seqnos[i]);
    CHECK(test_messages[static_cast<int64_t>(i)].header()->source_commit_timestamp == expected_commit_stamps[i]);
  }

  REQUIRE(ok(bridge.run_once(jewels::time::SyncTime{std::chrono::seconds(13)})));
  const auto status_messages = status_subscriber->available();
  REQUIRE(status_messages.size() == 1U);
  const auto& status_msg = *detail::marshal_as<const Tappy<BridgeStatus>>(
    std::span<const std::byte, sizeof(Tappy<BridgeStatus>)>{status_messages[0U].message()});
  CHECK(status_msg.get_host_name() == test_host_name);
  CHECK(status_msg.get_server_counters().size() == 2U);
  CHECK(status_msg.get_server_counters()[0U].get_channel_name() == "SERVER TOTAL");
  CHECK(status_msg.get_server_counters()[1U].get_channel_name() == "SERVER BULK DATA TOTAL");
  CHECK(status_msg.get_client_counters().size() == 3U);
  CHECK(status_msg.get_client_counters()[0U].get_channel_name() == test_channel_name);
  CHECK(status_msg.get_client_counters()[1U].get_channel_name() == "CLIENT TOTAL");
  CHECK(status_msg.get_client_counters()[2U].get_channel_name() == "CLIENT BULK DATA TOTAL");
  if (is_bulk_data)
  {
    CHECK(status_msg.get_client_counters()[1U].get_message_rate_hz() == 0.0);
    CHECK(status_msg.get_client_counters()[2U].get_message_rate_hz() > 0.0);
  }
  else
  {
    CHECK(status_msg.get_client_counters()[1U].get_message_rate_hz() > 0.0);
    CHECK(status_msg.get_client_counters()[2U].get_message_rate_hz() == 0.0);
  }
  bridge.request_stop();
  ::close(accepted);
}

TEST_CASE("TcpBridgeServer")
{
  const auto is_bulk_data = GENERATE(false, true);
  CAPTURE(is_bulk_data);
  using Msg = uint32_t;
  constexpr size_t num_slots = 3U;
  constexpr size_t max_observers = 1U;
  constexpr auto test_channel_name = "test_channel";

  const pinion::BufferLayout test_buffer_layout{
    .num_slots = num_slots,
    .message_size = sizeof(Msg),
    .is_published_once = false,
  };

  const pinion::BufferLayout status_buffer_layout{
    .num_slots = num_slots,
    .message_size = sizeof(Tappy<BridgeStatus>),
    .is_published_once = false,
  };

  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  jewels::testing::TmpDirectoryGuard tmp_dir_guard;
  const auto& test_dir = tmp_dir_guard.get_path().string();
  const auto nmsp = jewels::Uuid<common::EndpointInstanceId>::random_uuid().to_string();

  auto channel_factory_result = ShmChannelFactory::make(memres, nmsp, test_dir);
  REQUIRE(channel_factory_result);
  auto [bound_socket, bound_addr] = support::make_bound_socket();

  // Create the bridge.
  const auto config_ptr = std::make_unique<Tappy<TcpBridgeConfig<>>>();
  auto& config = *config_ptr;
  config.get_underlying_host_name().set_truncate(test_host_name);
  const auto test_channel_uuid = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  auto& server_config = config.get_underlying_bridge_servers().emplace_back();
  server_config.set_publisher_id(test_channel_uuid);
  server_config.get_mutable_buffer_layout().set_num_slots(num_slots);
  server_config.get_mutable_buffer_layout().set_message_size(sizeof(Msg));
  server_config.get_mutable_buffer_layout().set_is_published_once(false);
  server_config.get_underlying_listen_address().set_truncate(support::local_socket_host);
  server_config.set_listen_port(::ntohs(bound_addr.port()));
  server_config.set_num_clients(1U);
  server_config.get_underlying_channel_name().set_truncate(test_channel_name);
  const auto status_channel_uuid = jewels::Uuid<common::EndpointInstanceId>::random_uuid();
  auto& status_config = config.get_mutable_status_publish_endpoint();
  status_config.set_publisher_id(status_channel_uuid);
  status_config.get_underlying_channel_name().set_truncate(test_channel_name);
  status_config.get_mutable_buffer_layout().set_num_slots(num_slots);
  status_config.get_mutable_buffer_layout().set_message_size(sizeof(Tappy<BridgeStatus>));
  status_config.get_mutable_buffer_layout().set_is_published_once(false);
  TcpBridge bridge{
    memres,
    std::make_shared<ShmChannelFactory>(*std::move(channel_factory_result)),
    config,
    jewels::time::SyncTime{std::chrono::seconds(1)}};
  REQUIRE(ok(bridge.initialize(config)));
  CHECK(bridge.get_num_servers() == 1U);

  auto status_subscriber_result = bridge.channel_factory().open_subscriber(
    status_channel_uuid.to_string(), status_channel_name, status_buffer_layout, max_observers);
  REQUIRE(status_subscriber_result);
  auto status_subscriber = status_subscriber_result.value();

  auto test_publisher_result = bridge.channel_factory().open_publisher(
    test_channel_uuid.to_string(), test_channel_name, test_buffer_layout, max_observers);
  REQUIRE(test_publisher_result);

  // Run the server to check for connections
  REQUIRE(ok(bridge.run_once(jewels::time::SyncTime{std::chrono::seconds(1)})));

  const auto ephemeral_addr = jewels::networking::SocketAddress::create(std::string{support::local_socket_host}, 0);
  auto tcp_client = TcpSocket::create_connect(bound_addr, *ephemeral_addr);

  REQUIRE(tcp_client);
  // Set non-blocking so we can fail fast if the server is busted.
  REQUIRE(set_nonblocking(tcp_client->descriptor(), true));

  /// Run the bridge server to complete the connection
  REQUIRE(ok(bridge.run_once(jewels::time::SyncTime{std::chrono::seconds(1)})));

  // Receive a null header to verify that the server is connected
  CHECK(support::recv_null_header(tcp_client->descriptor(), 0U));

  REQUIRE(ok(bridge.run_once(jewels::time::SyncTime{std::chrono::seconds(13)})));
  const auto status_messages = status_subscriber->available();
  REQUIRE(status_messages.size() == 1U);
  const auto& status_msg = *detail::marshal_as<const Tappy<BridgeStatus>>(
    std::span<const std::byte, sizeof(Tappy<BridgeStatus>)>{status_messages[0U].message()});
  CHECK(status_msg.get_host_name() == test_host_name);
  CHECK(status_msg.get_server_counters().size() == 2U);
  CHECK(status_msg.get_server_counters()[0U].get_channel_name() == "SERVER TOTAL");
  CHECK(status_msg.get_server_counters()[1U].get_channel_name() == "SERVER BULK DATA TOTAL");
  CHECK(status_msg.get_client_counters().size() == 2U);
  CHECK(status_msg.get_client_counters()[0U].get_channel_name() == "CLIENT TOTAL");
  CHECK(status_msg.get_client_counters()[1U].get_channel_name() == "CLIENT BULK DATA TOTAL");
}

} // namespace clockwork::pinion
