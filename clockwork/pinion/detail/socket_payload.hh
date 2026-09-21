// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/io/network_var_packet_clk_cc.hh"
#include "clockwork/io/var_packet_clk_cc.hh"
#include "jewels/std/expected.hh"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace clockwork::pinion::detail
{

/// Get a writable byte span to the schema.
/// @param msg The input message.
/// @return A byte span referring to the entire message.
template <class Schema>
std::span<std::byte, sizeof(Tachyon<Schema>)> as_writable_byte_span(Tachyon<Schema>& msg);

/// Get a writable byte span to the payload within the udp packet schema.
/// @param msg The input message.
/// @return A byte span referring to the bytes field within the message.
template <auto packet_size>
std::span<std::byte, packet_size> as_writable_byte_span(Tachyon<io::VarPacket<packet_size>>& msg);

/// Get a writable byte span to the payload within the network packet schema.
/// @param msg The input message.
/// @return A byte span referring to the bytes field within the message.
template <auto packet_size>
std::span<std::byte, packet_size> as_writable_byte_span(Tachyon<io::NetworkVarPacket<packet_size>>& msg);

/// Get a non writable byte span to the schema.
/// @note These still take in mutable objects and return a mutable
/// span because iovec requires a non-const pointer.  However, they do
/// not mutate any data like the as_writable_* counterparts do.
/// @param msg The input message.
/// @return A byte span referring to the entire message.
template <class Schema>
std::span<std::byte, sizeof(Tachyon<Schema>)> as_byte_span(Tachyon<Schema>& msg);

/// Get a non writable byte span to the payload within the udp packet schema.
/// @note These still take in mutable objects and return a mutable
/// span because iovec requires a non-const pointer.  However, they do
/// not mutate any data like the as_writable_* counterparts do.
/// @param msg The input message.
/// @return A byte span referring to the bytes field within the message.
template <auto packet_size>
std::span<std::byte> as_byte_span(Tachyon<io::VarPacket<packet_size>>& msg);

/// Get a non writable byte span to the payload within the network packet schema.
/// @note These still take in mutable objects and return a mutable
/// span because iovec requires a non-const pointer.  However, they do
/// not mutate any data like the as_writable_* counterparts do.
/// @param msg The input message.
/// @return A byte span referring to the bytes field within the message.
template <auto packet_size>
std::span<std::byte> as_byte_span(Tachyon<io::NetworkVarPacket<packet_size>>& msg);

/// Populate the address fields of the schema.
/// @param msg The input message.
/// @param src_address The ip address of the source.
/// @param src_port The ip port number of the source.
/// @param dst_address The ip address of the destination.
/// @param dst_port The ip port number of the destination.
/// @return Error if we fail to populate the fields due to a string/buffer size mismatch.
template <class Schema>
jewels::expected<void, jewels::MonoError> populate_address_fields(
  Tachyon<Schema>& msg,
  std::string_view src_address,
  uint16_t src_port,
  std::string_view dst_address,
  uint16_t dst_port);

/// Populate the address fields of a network packet schema.
/// @param msg The input message.
/// @param src_address The ip address of the source.
/// @param src_port The ip port number of the source.
/// @param dst_address The ip address of the destination.
/// @param dst_port The ip port number of the destination.
/// @return Error if we fail to populate the fields due to a string/buffer size mismatch.
template <auto packet_size>
jewels::expected<void, jewels::MonoError> populate_address_fields(
  Tachyon<io::NetworkVarPacket<packet_size>>& msg,
  std::string_view src_address,
  uint16_t src_port,
  std::string_view dst_address,
  uint16_t dst_port);
} // namespace clockwork::pinion::detail

#include "clockwork/pinion/detail/socket_payload.inl"
