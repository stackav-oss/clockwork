// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/tests/support/sockets.hh"

#include "jewels/networking/socket_address.hh"

#include <arpa/inet.h>

#include <cerrno>
#include <cstdint>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <utility>

namespace clockwork::support
{

jewels::expected<void, jewels::filesystem::ErrorCode> wait_for_readable(int fd_to_wait_on)
{
  auto epoll_fd = ::epoll_create1(0);
  if (epoll_fd < 0)
  {
    return jewels::unexpected{jewels::filesystem::make_error_code(errno)};
  }
  ::epoll_event event{};
  event.events = EPOLLIN;
  event.data.fd = fd_to_wait_on;

  if (::epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd_to_wait_on, &event) < 0)
  {
    return jewels::unexpected{jewels::filesystem::make_error_code(errno)};
  }

  // In unit tests, its expected a socket is ready to read immediately
  // after write.  In Linux, this isn't necessarily guaranteed.  Add a
  // short epoll_wait to allow for the kernel to process the read.
  const auto timeout = std::chrono::milliseconds{100};
  using TimeoutType = std::chrono::duration<int, std::chrono::milliseconds::period>;
  const auto num_events = ::epoll_wait(epoll_fd, &event, 1, std::chrono::duration_cast<TimeoutType>(timeout).count());
  if (num_events < 0)
  {
    return jewels::unexpected{jewels::filesystem::make_error_code(errno)};
  }
  if (num_events == 0)
  {
    return jewels::unexpected{jewels::filesystem::make_error_code(EAGAIN)};
  }
  return {};
}

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

Sender::Sender()
  : file_descriptor_{::socket(AF_INET, SOCK_DGRAM, 0)}
{
  if (!file_descriptor_)
  {
    throw std::runtime_error{"Failed to construct socket fd"};
  }
}

jewels::expected<size_t, jewels::filesystem::ErrorCode>
Sender::operator()(std::span<const std::byte> data, struct sockaddr_in const& addr) const
{
  return send_to(data, *file_descriptor_, addr);
}

int Sender::fd() const
{
  return *file_descriptor_;
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
  return get_port(*received_addr);
}

Receiver::Receiver(jewels::filesystem::FileDescriptor&& file_descriptor)
  : file_descriptor_{std::move(file_descriptor)}
{
}

[[nodiscard]] int Receiver::fd()
{
  return *file_descriptor_;
}

void populate_value(Tachyon<JustAUInt32>& message, uint32_t payload)
{
  message.value = payload;
}

uint16_t get_port(struct sockaddr_in const& addr)
{
  return ::ntohs(addr.sin_port);
}

} // namespace clockwork::support
