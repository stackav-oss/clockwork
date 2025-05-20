// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/io/network_var_packet.hh"
#include "clockwork/io/var_packet.hh"

#include <cstddef>
#include <cstdint>
#include <span>

namespace clockwork
{

/// A mock schema tag type with just a uint32_t.
struct JustAUInt32;

/// Tachyon representation of JustAUInt32.
template <>
struct Tachyon<clockwork::JustAUInt32>
{
  uint32_t value;
};

namespace pinion
{

/// Convert JustAUInt32 to a byte span.
inline std::span<const std::byte, sizeof(uint32_t)> msg_as_byte_span(const Tachyon<JustAUInt32>& msg);

/// Convert bytes payload within VarPacket to a byte span.
template <size_t payload_size>
std::span<const std::byte> msg_as_byte_span(const Tachyon<io::VarPacket<payload_size>>& msg);

/// Convert bytes payload within NetworkVarPacket to a byte span.
template <size_t payload_size>
std::span<const std::byte> msg_as_byte_span(const Tachyon<io::NetworkVarPacket<payload_size>>& msg);

/// Check if a schema tag is a VarPacket.  Base case.
template <class Schema>
inline constexpr bool is_var_packet_v{false};

/// Specialization for VarPacket.
template <size_t payload_size>
inline constexpr bool is_var_packet_v<io::VarPacket<payload_size>>{true};

/// Check if a schema tag is a NetworkVarPacket.  Base case.
template <class Schema>
inline constexpr bool is_network_var_packet_v{false};

/// Specialization for NetworkVarPacket.
template <size_t payload_size>
inline constexpr bool is_network_var_packet_v<io::NetworkVarPacket<payload_size>>{true};

} // namespace pinion
} // namespace clockwork

#include "clockwork/pinion/tests/support/udp_payloads.inl"
