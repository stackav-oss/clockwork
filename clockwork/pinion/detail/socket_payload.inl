// IWYU pragma: private, include "clockwork/pinion/detail/socket_payload.hh"

#include <span>
#pragma once

#include "clockwork/io/network_var_packet.hh"
#include "clockwork/io/var_packet.hh"
#include "clockwork/pinion/detail/socket_payload.hh"
#include "jewels/std/expected.hh"
#include "jewels/std/span.hh"

#include <arpa/inet.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <netinet/in.h>
#include <string_view>
#include <sys/socket.h>

namespace clockwork::pinion::detail
{
template <class Schema>
std::span<std::byte, sizeof(Tachyon<Schema>)> as_writable_byte_span(Tachyon<Schema>& msg)
{
  return as_writable_bytes(jewels::as_single_item_span(msg));
}

template <auto packet_size>
std::span<std::byte, packet_size> as_writable_byte_span(Tachyon<io::VarPacket<packet_size>>& msg)
{
  msg.bytes.resize(packet_size);
  return std::span<std::byte, packet_size>{msg.bytes};
}

template <auto packet_size>
std::span<std::byte, packet_size> as_writable_byte_span(Tachyon<io::NetworkVarPacket<packet_size>>& msg)
{
  msg.bytes.resize(packet_size);
  return std::span<std::byte, packet_size>{msg.bytes};
}

template <class Schema>
std::span<std::byte, sizeof(Tachyon<Schema>)> as_byte_span(Tachyon<Schema>& msg)
{
  // Reuse the writable version as the non-VarPacket type is just a
  // span of the entire object.  No need to do anything different
  // between the writable and non-writable versions.
  return as_writable_byte_span(msg);
}

template <auto packet_size>
std::span<std::byte> as_byte_span(Tachyon<io::VarPacket<packet_size>>& msg)
{
  return std::span{msg.bytes};
}

template <auto packet_size>
std::span<std::byte> as_byte_span(Tachyon<io::NetworkVarPacket<packet_size>>& msg)
{
  return std::span{msg.bytes};
}

template <class Schema>
jewels::expected<void, jewels::MonoError> populate_address_fields(
  Tachyon<Schema>& /*msg*/, const ::sockaddr_in& /*src_addr*/, std::string_view /*dst_address*/, uint16_t /*dst_port*/)
{
  return {};
}

template <auto packet_size>
jewels::expected<void, jewels::MonoError> populate_address_fields(
  Tachyon<io::NetworkVarPacket<packet_size>>& msg,
  const ::sockaddr_in& src_addr,
  std::string_view dst_address,
  uint16_t dst_port)
{
  std::array<char, INET_ADDRSTRLEN> src_address_buffer{};
  const char* ntop_result = inet_ntop(AF_INET, &src_addr.sin_addr, src_address_buffer.data(), INET_ADDRSTRLEN);
  if (ntop_result != src_address_buffer.data())
  {
    return jewels::unexpected(jewels::MonoError{});
  }

  // inet_ntop guarantees null-termination on success, so we can safely use the result
  const auto src_address = std::string_view(ntop_result);
  const auto src_port = ntohs(src_addr.sin_port);
  if (msg.src_address.capacity() < src_address.size())
  {
    return jewels::unexpected(jewels::MonoError{});
  }

  if (msg.dst_address.capacity() < dst_address.size())
  {
    return jewels::unexpected(jewels::MonoError{});
  }

  msg.src_address.resize(src_address.size());
  std::copy(src_address.begin(), src_address.end(), msg.src_address.begin());
  msg.src_port = src_port;

  msg.dst_address.resize(dst_address.size());
  std::copy(dst_address.begin(), dst_address.end(), msg.dst_address.begin());
  msg.dst_port = dst_port;

  return {};
}
} // namespace clockwork::pinion::detail
