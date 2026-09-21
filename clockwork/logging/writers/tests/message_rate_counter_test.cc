// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/writers/channel_message_rates_config_clk_cc.hh"
#include "clockwork/logging/writers/message_rate_counter.hh"
#include "clockwork/logging/writers/rate_filter.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/container/tap/var_array.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/time/sync_time.hh"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <functional>
#include <memory_resource>
#include <string_view>

namespace clockwork_logging
{
namespace
{

TEST_CASE("MessageRateCounter")
{
  constexpr auto window_size_sec = 35;
  constexpr auto no_min_rate_channel = "no/min/rate/channel";
  constexpr auto min_rate_channel = "/min/rate/channel";
  constexpr jewels::time::SteadyTime logtime1{std::chrono::seconds(1)};
  constexpr jewels::time::SteadyTime logtime2{std::chrono::seconds(2)};
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};

  clockwork::Tappy<ChannelMessageRatesConfig> channel_rates_config;
  channel_rates_config.set_window_size_sec(window_size_sec);
  auto& channel_message_rate = channel_rates_config.get_underlying_channel_message_rates().emplace_back();
  channel_message_rate.get_underlying_channel_name().set_truncate(min_rate_channel);
  channel_message_rate.set_min_msg_rate_hz(1.0);
  MessageRateCounter rate_counter{memory_resource, channel_rates_config};

  SECTION("Channels without minimum rates default to zero hz")
  {
    rate_counter.add_channel(no_min_rate_channel, logtime1);
    REQUIRE(rate_counter.get_channel_rate_map().contains(no_min_rate_channel));
    REQUIRE(rate_counter.get_channel_rate_map().contains(no_min_rate_channel));
    REQUIRE(rate_counter.get_channel_rate_map().at(no_min_rate_channel).min_msg_rate_hz == 0.0);
    rate_counter.update_channel(no_min_rate_channel, logtime1, 105U);
    REQUIRE(rate_counter.get_channel_rate_map().at(no_min_rate_channel).rate_filter.get_rate(logtime2) == 3.0);
  }

  SECTION("Channels get the minimum rates from the initializer map")
  {
    rate_counter.add_channel(min_rate_channel, logtime1);
    REQUIRE(rate_counter.get_channel_rate_map().contains(min_rate_channel));
    REQUIRE(rate_counter.get_channel_rate_map().at(min_rate_channel).min_msg_rate_hz == 1.0);
    rate_counter.update_channel(min_rate_channel, logtime1, 105U);
    REQUIRE(rate_counter.get_channel_rate_map().at(min_rate_channel).rate_filter.get_rate(logtime2) == 3.0);
  }

  SECTION("Warmup interval")
  {
    const auto warmup_interval = std::chrono::nanoseconds(1'000'000'000);
    const auto start_time = jewels::time::SteadyClock::now();
    const MessageRateCounter warm_rate_counter{memory_resource, channel_rates_config, warmup_interval, start_time};
    REQUIRE_FALSE(warm_rate_counter.is_warmed_up(start_time));
    REQUIRE_FALSE(warm_rate_counter.is_warmed_up(start_time + warmup_interval - std::chrono::nanoseconds(1)));
    REQUIRE(warm_rate_counter.is_warmed_up(start_time + warmup_interval));
  }
}

} // namespace
} // namespace clockwork_logging
