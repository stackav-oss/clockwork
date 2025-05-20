// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/tests/support/sockets.hh"

#include "jewels/networking/socket_address.hh"

#include <arpa/inet.h>

#include <cerrno>
#include <cstdint>
#include <netinet/in.h>
#include <sys/socket.h>
#include <utility>

namespace clockwork::support
{

jewels::expected<struct sockaddr_in, jewels::filesystem::ErrorCode> get_assigned_addr(int file_descriptor)
{
  struct sockaddr_in assigned_addr{};
  uint32_t len{sizeof(assigned_addr)};
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) c-style inheritence
  if (::getsockname(file_descriptor, reinterpret_cast<struct sockaddr*>(&assigned_addr), &len) == 0)
  {
    return {assigned_addr};
  }
  return jewels::unexpected{jewels::filesystem::make_error_code(errno)};
}

jewels::expected<size_t, jewels::filesystem::ErrorCode>
send_to(std::span<const std::byte> data, int file_descriptor, struct sockaddr_in const& addr)
{
  const auto send_length = ::sendto(
    file_descriptor,
    data.data(),
    data.size(),
    0,
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) c-style inheritence
    reinterpret_cast<struct sockaddr const*>(&addr),
    sizeof(addr));
  if (send_length == -1)
  {
    return jewels::unexpected{jewels::filesystem::make_error_code(errno)};
  }
  return {static_cast<size_t>(send_length)};
}

[[nodiscard]] jewels::expected<Receiver, jewels::filesystem::ErrorCode>
Receiver::try_make(const std::string& host, uint16_t port)
{
  const auto addr = jewels::networking::SocketAddress::create(host, port);
  if (!addr)
  {
    return jewels::unexpected{addr.error()};
  }
  jewels::filesystem::FileDescriptor file_descriptor{::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0)};
  if (::bind(*file_descriptor, addr->ptr(), jewels::networking::SocketAddress::byte_size()) != 0)
  {
    return jewels::unexpected{jewels::filesystem::make_error_code(errno)};
  }
  return Receiver{std::move(file_descriptor)};
}

[[nodiscard]] jewels::expected<uint16_t, jewels::filesystem::ErrorCode> Receiver::port() const
{
  const auto received_addr = support::get_assigned_addr(*file_descriptor_);
  if (!received_addr)
  {
    return jewels::unexpected{received_addr.error()};
  }
  return ::ntohs(received_addr->sin_port);
}

Receiver::Receiver(jewels::filesystem::FileDescriptor&& file_descriptor)
  : file_descriptor_{std::move(file_descriptor)}
{
}

void populate_value(Tachyon<JustAUInt32>& message, uint32_t payload)
{
  message.value = payload;
}

} // namespace clockwork::support
