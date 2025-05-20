// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/bridge_status.hh"
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
}

} // namespace
} // namespace clockwork::pinion
