// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/bridge_status_clk_cc.hh"
#include "clockwork/pinion/tcp_bridge_common.hh"
#include "clockwork/repr_iface.hh"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <string_view>

namespace clockwork::pinion
{
namespace
{

TEST_CASE("combine_client_server_counters")
{
  TcpBridgeClientServerCounters counters1{
    .message_count = 11U,
    .message_bytes = 12U,
    .compressed_message_bytes = 13U,
    .total_receive_latency = std::chrono::nanoseconds(14),
    .total_transfer_time = std::chrono::nanoseconds(15),
    .total_compression_time = std::chrono::nanoseconds(16),
    .total_bridge_latency = std::chrono::nanoseconds(17),
    .max_receive_latency = std::chrono::nanoseconds(18),
    .max_transfer_time = std::chrono::nanoseconds(19),
    .max_compression_time = std::chrono::nanoseconds(110),
    .max_bridge_latency = std::chrono::nanoseconds(120),
    .null_header_count = 20U,
    .keep_alive_count = 21U,
  };

  TcpBridgeClientServerCounters counters2{
    .message_count = 21U,
    .message_bytes = 22U,
    .compressed_message_bytes = 23U,
    .total_receive_latency = std::chrono::nanoseconds(24),
    .total_transfer_time = std::chrono::nanoseconds(25),
    .total_compression_time = std::chrono::nanoseconds(26),
    .total_bridge_latency = std::chrono::nanoseconds(27),
    .max_receive_latency = std::chrono::nanoseconds(28),
    .max_transfer_time = std::chrono::nanoseconds(29),
    .max_compression_time = std::chrono::nanoseconds(210),
    .max_bridge_latency = std::chrono::nanoseconds(220),
    .null_header_count = 30U,
    .keep_alive_count = 31U,
  };

  SECTION("1 into 2")
  {
    combine_client_server_counters(counters1, counters2);

    CHECK(counters2.message_count == 32U);
    CHECK(counters2.message_bytes == 34U);
    CHECK(counters2.compressed_message_bytes == 36U);
    CHECK(counters2.total_receive_latency.count() == 38);
    CHECK(counters2.total_transfer_time.count() == 40);
    CHECK(counters2.total_compression_time.count() == 42);
    CHECK(counters2.total_bridge_latency.count() == 44);
    CHECK(counters2.max_receive_latency.count() == 28);
    CHECK(counters2.max_transfer_time.count() == 29);
    CHECK(counters2.max_compression_time.count() == 210);
    CHECK(counters2.max_bridge_latency.count() == 220);
    CHECK(counters2.null_header_count == 50);
    CHECK(counters2.keep_alive_count == 52);
  }

  SECTION("2 into 1")
  {
    combine_client_server_counters(counters2, counters1);

    CHECK(counters1.message_count == 32U);
    CHECK(counters1.message_bytes == 34U);
    CHECK(counters1.compressed_message_bytes == 36U);
    CHECK(counters1.total_receive_latency.count() == 38);
    CHECK(counters1.total_transfer_time.count() == 40);
    CHECK(counters1.total_compression_time.count() == 42);
    CHECK(counters1.total_bridge_latency.count() == 44);
    CHECK(counters1.max_receive_latency.count() == 28);
    CHECK(counters1.max_transfer_time.count() == 29);
    CHECK(counters1.max_compression_time.count() == 210);
    CHECK(counters1.max_bridge_latency.count() == 220);
    CHECK(counters1.null_header_count == 50);
    CHECK(counters1.keep_alive_count == 52);
  }
}

TEST_CASE("store_client_server_counters")
{
  const TcpBridgeClientServerCounters counters{
    .message_count = 20U,
    .message_bytes = 30U,
    .compressed_message_bytes = 40U,
    .total_receive_latency = std::chrono::nanoseconds(500),
    .total_transfer_time = std::chrono::nanoseconds(600),
    .total_compression_time = std::chrono::nanoseconds(700),
    .total_bridge_latency = std::chrono::nanoseconds(800),
    .max_receive_latency = std::chrono::nanoseconds(90),
    .max_transfer_time = std::chrono::nanoseconds(100),
    .max_compression_time = std::chrono::nanoseconds(110),
    .max_bridge_latency = std::chrono::nanoseconds(120),
    .null_header_count = 130U,
    .keep_alive_count = 140U,
  };

  Tappy<BridgeClientServerCounters> bridge_counters{};
  store_client_server_counters("TEST_CHANNEL", counters, std::chrono::seconds(2), bridge_counters);

  CHECK(bridge_counters.get_channel_name() == "TEST_CHANNEL");
  CHECK(bridge_counters.get_message_rate_hz() == 10.0f);
  CHECK(bridge_counters.get_data_rate_bps() == 15.0f);
  CHECK(bridge_counters.get_compressed_data_rate_bps() == 20.0f);
  CHECK(bridge_counters.get_compression_ratio() == 0.75f);
  CHECK(bridge_counters.get_average_receive_latency().count() == 25);
  CHECK(bridge_counters.get_average_transfer_time().count() == 30);
  CHECK(bridge_counters.get_average_compression_time().count() == 35);
  CHECK(bridge_counters.get_average_bridge_latency().count() == 40);
  CHECK(bridge_counters.get_max_receive_latency().count() == 90);
  CHECK(bridge_counters.get_max_transfer_time().count() == 100);
  CHECK(bridge_counters.get_max_compression_time().count() == 110);
  CHECK(bridge_counters.get_max_bridge_latency().count() == 120);
  CHECK(bridge_counters.get_null_header_rate_hz() == 65.0f);
  CHECK(bridge_counters.get_keep_alive_rate_hz() == 70.0f);
}

TEST_CASE("diagnostics state")
{
  TcpBridgeDiagnosticsState diag_state;
  REQUIRE(diag_state.get_and_reset_counters() == TcpBridgeDiagnosticsCounters{});

  SECTION("drop_count")
  {
    diag_state.increment_drop_count();
    REQUIRE(diag_state.get_and_reset_counters() == TcpBridgeDiagnosticsCounters{.drop_count = 1U});
    diag_state.increment_drop_count(2U);
    REQUIRE(diag_state.get_and_reset_counters() == TcpBridgeDiagnosticsCounters{.drop_count = 2U});
    REQUIRE(diag_state.get_and_reset_counters() == TcpBridgeDiagnosticsCounters{});
  }

  SECTION("failed_sends")
  {
    diag_state.increment_failed_sends();
    REQUIRE(diag_state.get_and_reset_counters() == TcpBridgeDiagnosticsCounters{.failed_sends = 1U});
    REQUIRE(diag_state.get_and_reset_counters() == TcpBridgeDiagnosticsCounters{});
    diag_state.increment_failed_sends(2U);
    REQUIRE(diag_state.get_and_reset_counters() == TcpBridgeDiagnosticsCounters{.failed_sends = 2U});
  }

  SECTION("closed_socket_count")
  {
    diag_state.increment_closed_socket_count();
    REQUIRE(diag_state.get_and_reset_counters() == TcpBridgeDiagnosticsCounters{.closed_socket_count = 1U});
    REQUIRE(diag_state.get_and_reset_counters() == TcpBridgeDiagnosticsCounters{});
    diag_state.increment_closed_socket_count(2U);
    REQUIRE(diag_state.get_and_reset_counters() == TcpBridgeDiagnosticsCounters{.closed_socket_count = 2U});
  }

  SECTION("failed_recvs")
  {
    diag_state.increment_failed_recvs();
    REQUIRE(diag_state.get_and_reset_counters() == TcpBridgeDiagnosticsCounters{.failed_recvs = 1U});
    REQUIRE(diag_state.get_and_reset_counters() == TcpBridgeDiagnosticsCounters{});
    diag_state.increment_failed_recvs(2U);
    REQUIRE(diag_state.get_and_reset_counters() == TcpBridgeDiagnosticsCounters{.failed_recvs = 2U});
  }

  SECTION("failed_reservations")
  {
    diag_state.increment_failed_reservations();
    REQUIRE(diag_state.get_and_reset_counters() == TcpBridgeDiagnosticsCounters{.failed_reservations = 1U});
    REQUIRE(diag_state.get_and_reset_counters() == TcpBridgeDiagnosticsCounters{});
    diag_state.increment_failed_reservations(2U);
    REQUIRE(diag_state.get_and_reset_counters() == TcpBridgeDiagnosticsCounters{.failed_reservations = 2U});
  }

  SECTION("malformed_messages")
  {
    diag_state.increment_malformed_messages();
    REQUIRE(diag_state.get_and_reset_counters() == TcpBridgeDiagnosticsCounters{.malformed_messages = 1U});
    REQUIRE(diag_state.get_and_reset_counters() == TcpBridgeDiagnosticsCounters{});
    diag_state.increment_malformed_messages(2U);
    REQUIRE(diag_state.get_and_reset_counters() == TcpBridgeDiagnosticsCounters{.malformed_messages = 2U});
  }

  SECTION("failed_commits")
  {
    diag_state.increment_failed_commits();
    REQUIRE(diag_state.get_and_reset_counters() == TcpBridgeDiagnosticsCounters{.failed_commits = 1U});
    REQUIRE(diag_state.get_and_reset_counters() == TcpBridgeDiagnosticsCounters{});
    diag_state.increment_failed_commits(2U);
    REQUIRE(diag_state.get_and_reset_counters() == TcpBridgeDiagnosticsCounters{.failed_commits = 2U});
  }

  SECTION("failed_discards")
  {
    diag_state.increment_failed_discards();
    REQUIRE(diag_state.get_and_reset_counters() == TcpBridgeDiagnosticsCounters{.failed_discards = 1U});
    REQUIRE(diag_state.get_and_reset_counters() == TcpBridgeDiagnosticsCounters{});
    diag_state.increment_failed_discards(2U);
    REQUIRE(diag_state.get_and_reset_counters() == TcpBridgeDiagnosticsCounters{.failed_discards = 2U});
  }

  SECTION("client_socket_errors")
  {
    diag_state.increment_client_socket_errors();
    REQUIRE(diag_state.get_and_reset_counters() == TcpBridgeDiagnosticsCounters{.client_socket_errors = 1U});
    REQUIRE(diag_state.get_and_reset_counters() == TcpBridgeDiagnosticsCounters{});
    diag_state.increment_client_socket_errors(2U);
    REQUIRE(diag_state.get_and_reset_counters() == TcpBridgeDiagnosticsCounters{.client_socket_errors = 2U});
  }

  SECTION("progress_errors")
  {
    diag_state.increment_progress_errors();
    REQUIRE(diag_state.get_and_reset_counters() == TcpBridgeDiagnosticsCounters{.progress_errors = 1U});
    REQUIRE(diag_state.get_and_reset_counters() == TcpBridgeDiagnosticsCounters{});
    diag_state.increment_progress_errors(2U);
    REQUIRE(diag_state.get_and_reset_counters() == TcpBridgeDiagnosticsCounters{.progress_errors = 2U});
  }

  SECTION("epoll_errors")
  {
    diag_state.increment_epoll_errors();
    REQUIRE(diag_state.get_and_reset_counters() == TcpBridgeDiagnosticsCounters{.epoll_errors = 1U});
    REQUIRE(diag_state.get_and_reset_counters() == TcpBridgeDiagnosticsCounters{});
    diag_state.increment_epoll_errors(2U);
    REQUIRE(diag_state.get_and_reset_counters() == TcpBridgeDiagnosticsCounters{.epoll_errors = 2U});
  }

  SECTION("status_errors")
  {
    diag_state.increment_status_errors();
    REQUIRE(diag_state.get_and_reset_counters() == TcpBridgeDiagnosticsCounters{.status_errors = 1U});
    REQUIRE(diag_state.get_and_reset_counters() == TcpBridgeDiagnosticsCounters{});
    diag_state.increment_status_errors(2U);
    REQUIRE(diag_state.get_and_reset_counters() == TcpBridgeDiagnosticsCounters{.status_errors = 2U});
  }

  SECTION("max_bridge_latency")
  {
    diag_state.update_max_bridge_latency(std::chrono::nanoseconds(100), "channel1");
    REQUIRE(
      diag_state.get_and_reset_counters() ==
      TcpBridgeDiagnosticsCounters{
        .max_bridge_latency = std::chrono::nanoseconds(100), .max_latency_channel_name = "channel1"});
    REQUIRE(diag_state.get_and_reset_counters() == TcpBridgeDiagnosticsCounters{});
    diag_state.update_max_bridge_latency(std::chrono::nanoseconds(200), "channel2");
    diag_state.update_max_bridge_latency(std::chrono::nanoseconds(300), "channel3");
    diag_state.update_max_bridge_latency(std::chrono::nanoseconds(100), "channel1");
    REQUIRE(
      diag_state.get_and_reset_counters() ==
      TcpBridgeDiagnosticsCounters{
        .max_bridge_latency = std::chrono::nanoseconds(300), .max_latency_channel_name = "channel3"});
    REQUIRE(diag_state.get_and_reset_counters() == TcpBridgeDiagnosticsCounters{});
  }

  SECTION("max_bridge_bulk_datalatency")
  {
    diag_state.update_max_bridge_bulk_data_latency(std::chrono::nanoseconds(100), "channel1");
    REQUIRE(
      diag_state.get_and_reset_counters() == TcpBridgeDiagnosticsCounters{
                                               .max_bridge_bulk_data_latency = std::chrono::nanoseconds(100),
                                               .max_bulk_data_latency_channel_name = "channel1"});
    REQUIRE(diag_state.get_and_reset_counters() == TcpBridgeDiagnosticsCounters{});
    diag_state.update_max_bridge_bulk_data_latency(std::chrono::nanoseconds(200), "channel2");
    diag_state.update_max_bridge_bulk_data_latency(std::chrono::nanoseconds(300), "channel3");
    diag_state.update_max_bridge_bulk_data_latency(std::chrono::nanoseconds(100), "channel1");
    REQUIRE(
      diag_state.get_and_reset_counters() == TcpBridgeDiagnosticsCounters{
                                               .max_bridge_bulk_data_latency = std::chrono::nanoseconds(300),
                                               .max_bulk_data_latency_channel_name = "channel3"});
    REQUIRE(diag_state.get_and_reset_counters() == TcpBridgeDiagnosticsCounters{});
  }
}

TEST_CASE("combine_diagnostics_counters")
{
  TcpBridgeDiagnosticsCounters dest{};
  TcpBridgeDiagnosticsCounters source{
    .drop_count = 1U,
    .failed_sends = 2U,
    .closed_socket_count = 3U,
    .failed_recvs = 4U,
    .failed_reservations = 5U,
    .malformed_messages = 6U,
    .failed_commits = 7U,
    .failed_discards = 8U,
    .client_socket_errors = 9U,
    .progress_errors = 10U,
    .epoll_errors = 11U,
    .status_errors = 12U,
    .max_bridge_latency = std::chrono::seconds(13),
    .max_latency_channel_name = "Channel14",
    .max_bridge_bulk_data_latency = std::chrono::seconds(15),
    .max_bulk_data_latency_channel_name = "Channel16",
  };

  combine_diagnostics_counters(source, dest);
  combine_diagnostics_counters(source, dest);

  REQUIRE(dest.drop_count == 2U);
  REQUIRE(dest.failed_sends == 4U);
  REQUIRE(dest.closed_socket_count == 6U);
  REQUIRE(dest.failed_recvs == 8U);
  REQUIRE(dest.failed_reservations == 10U);
  REQUIRE(dest.malformed_messages == 12U);
  REQUIRE(dest.failed_commits == 14U);
  REQUIRE(dest.failed_discards == 16U);
  REQUIRE(dest.client_socket_errors == 18U);
  REQUIRE(dest.progress_errors == 20U);
  REQUIRE(dest.epoll_errors == 22U);
  REQUIRE(dest.status_errors == 24U);
  REQUIRE(dest.max_bridge_latency == std::chrono::seconds(13));
  REQUIRE(dest.max_latency_channel_name == "Channel14");
  REQUIRE(dest.max_bridge_bulk_data_latency == std::chrono::seconds(15));
  REQUIRE(dest.max_bulk_data_latency_channel_name == "Channel16");

  source.max_bridge_latency = std::chrono::seconds(39);
  source.max_latency_channel_name = "Channel42";
  source.max_bridge_bulk_data_latency = std::chrono::seconds(45);
  source.max_bulk_data_latency_channel_name = "Channel48";

  combine_diagnostics_counters(source, dest);

  REQUIRE(dest.drop_count == 3U);
  REQUIRE(dest.failed_sends == 6U);
  REQUIRE(dest.closed_socket_count == 9U);
  REQUIRE(dest.failed_recvs == 12U);
  REQUIRE(dest.failed_reservations == 15U);
  REQUIRE(dest.malformed_messages == 18U);
  REQUIRE(dest.failed_commits == 21U);
  REQUIRE(dest.failed_discards == 24U);
  REQUIRE(dest.client_socket_errors == 27U);
  REQUIRE(dest.progress_errors == 30U);
  REQUIRE(dest.epoll_errors == 33U);
  REQUIRE(dest.status_errors == 36U);
  REQUIRE(dest.max_bridge_latency == std::chrono::seconds(39));
  REQUIRE(dest.max_latency_channel_name == "Channel42");
  REQUIRE(dest.max_bridge_bulk_data_latency == std::chrono::seconds(45));
  REQUIRE(dest.max_bulk_data_latency_channel_name == "Channel48");
}

} // namespace
} // namespace clockwork::pinion
