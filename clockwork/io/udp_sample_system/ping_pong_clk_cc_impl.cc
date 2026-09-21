// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dial/include_common.hh"
#include "clockwork/io/udp_sample_system/just_a_uint32_clk_cc.hh"
#include "clockwork/io/udp_sample_system/ping_pong_clk_cc_dial.hh"
#include "clockwork/io/var_packet_clk_cc.hh"
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/publishable.hh"
#include "clockwork/pinion/slot.hh"
#include "jewels/container/tap/var_array.hh"
#include "jewels/memory/bits.hh"

#include <fmt/base.h>

#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>

namespace clockwork::cogs
{
void execute_cog(PingCogDial& dial)
{
  static uint32_t count{0UL};

  auto& message = dial.get_outputs().get_ping_var_packet().message();
  message.get_underlying_bytes().resize(sizeof(count));
  jewels::memory::write_as_bytes(count, std::span<std::byte, sizeof(count)>{message.get_mutable_bytes()});
  dial.get_outputs().get_ping_var_packet().mark_for_publish();

  dial.get_outputs().get_ping_just_a_uint32().message().set_value(count);
  dial.get_outputs().get_ping_just_a_uint32().mark_for_publish();

  fmt::println("Ping: {}", count++);
}

void execute_cog(PongCogDial& dial)
{
  for (const auto& message : dial.get_inputs().get_pong_var_packet().get_new_msgs_view())
  {
    if (message.get_bytes().size() != sizeof(uint32_t))
    {
      fmt::println("Expected {} bytes, but received {} bytes.", sizeof(uint32_t), message.get_bytes().size());
      continue;
    }
    fmt::println(
      "Pong VarPacket: {}",
      jewels::memory::bit_cast_to<uint32_t>(std::span<const std::byte, sizeof(uint32_t)>{message.get_bytes()}));
  }
  for (const auto& message : dial.get_inputs().get_pong_just_a_uint32().get_new_msgs_view())
  {
    fmt::println("Pong JustAUInt32: {}", message.get_value());
  }
}
} // namespace clockwork::cogs
