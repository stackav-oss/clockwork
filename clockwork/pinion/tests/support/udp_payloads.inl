// IWYU pragma: private, include "clockwork/pinion/tests/support/udp_payloads.hh"

#pragma once

#include "clockwork/pinion/tests/support/udp_payloads.hh"

#include "clockwork/io/network_var_packet_clk_cc.hh"
#include "clockwork/io/var_packet_clk_cc.hh"
#include "jewels/std/span.hh"

#include <cstddef>
#include <cstdint>
#include <span>

namespace clockwork::pinion
{

inline std::span<const std::byte, sizeof(uint32_t)> msg_as_byte_span(const Tachyon<JustAUInt32>& msg)
{
  return as_bytes(jewels::as_single_item_span(msg));
}

template <size_t payload_size>
std::span<const std::byte> msg_as_byte_span(const Tachyon<io::VarPacket<payload_size>>& msg)
{
  return std::span{msg.bytes};
}

template <size_t payload_size>
std::span<const std::byte> msg_as_byte_span(const Tachyon<io::NetworkVarPacket<payload_size>>& msg)
{
  return std::span{msg.bytes};
}

} // namespace clockwork::pinion
