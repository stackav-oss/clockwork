// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/detail/tcp_socket.hh"

#include "clockwork/pinion/detail/socket_common.hh"
#include "jewels/filesystem/error_code.hh"

#include <cerrno>
#include <string>
#include <sys/socket.h>
#include <utility>

namespace clockwork::pinion
{

jewels::expected<TcpSocket, std::errc>
TcpSocket::create_listen(const jewels::networking::SocketAddress& sockaddr, int backlog)
{
  jewels::filesystem::FileDescriptor sock{::socket(AF_INET, SOCK_STREAM, 0)};
  if (!sock)
  {
    return jewels::unexpected(static_cast<std::errc>(errno));
  }
  const int reuse_addr_opt = 1;
  if (::setsockopt(*sock, SOL_SOCKET, SO_REUSEADDR, &reuse_addr_opt, sizeof(reuse_addr_opt)) != 0)
  {
    return jewels::unexpected(static_cast<std::errc>(errno));
  }
  if (::bind(*sock, sockaddr.ptr(), jewels::networking::SocketAddress::byte_size()) == -1)
  {
    return jewels::unexpected(static_cast<std::errc>(errno));
  }
  if (::listen(*sock, backlog) == -1)
  {
    return jewels::unexpected(static_cast<std::errc>(errno));
  }
  return TcpSocket(std::move(sock));
}

jewels::expected<TcpSocket, std::errc> TcpSocket::create_connect(
  const jewels::networking::SocketAddress& remote_addr, const jewels::networking::SocketAddress& local_addr)
{
  jewels::filesystem::FileDescriptor sock{::socket(AF_INET, SOCK_STREAM, 0)};
  if (!sock)
  {
    return jewels::unexpected(static_cast<std::errc>(errno));
  }
  if (::bind(*sock, local_addr.ptr(), jewels::networking::SocketAddress::byte_size()) == -1)
  {
    return jewels::unexpected(static_cast<std::errc>(errno));
  }
  if (::connect(*sock, remote_addr.ptr(), jewels::networking::SocketAddress::byte_size()) == -1)
  {
    return jewels::unexpected(static_cast<std::errc>(errno));
  }
  return TcpSocket(std::move(sock));
}

jewels::expected<TcpSocket, std::errc> TcpSocket::create_connect_async(
  const jewels::networking::SocketAddress& remote_addr, const jewels::networking::SocketAddress& local_addr)
{
  jewels::filesystem::FileDescriptor sock{::socket(AF_INET, SOCK_STREAM, 0)};
  if (!sock)
  {
    return jewels::unexpected(static_cast<std::errc>(errno));
  }
  if (::bind(*sock, local_addr.ptr(), jewels::networking::SocketAddress::byte_size()) == -1)
  {
    return jewels::unexpected(static_cast<std::errc>(errno));
  }
  if (!set_nonblocking(*sock, true))
  {
    return jewels::unexpected(static_cast<std::errc>(errno));
  }
  if (::connect(*sock, remote_addr.ptr(), jewels::networking::SocketAddress::byte_size()) == -1 && errno != EINPROGRESS)
  {
    return jewels::unexpected(static_cast<std::errc>(errno));
  }
  return TcpSocket(std::move(sock));
}

TcpSocket::TcpSocket(jewels::filesystem::FileDescriptor sock) noexcept
  : socket_(std::move(sock))
{
}

int TcpSocket::descriptor() const noexcept
{
  return *socket_;
}

jewels::filesystem::FileDescriptor TcpSocket::release_descriptor()
{
  return std::move(socket_);
}

[[nodiscard]] jewels::expected<jewels::networking::SocketAddress, std::errc>
TcpSocket::get_bound_address() const noexcept
{
  auto sockaddr = jewels::networking::SocketAddress::create(std::string{"0.0.0.0"}, 0);
  if (!sockaddr)
  {
    return jewels::unexpected(std::errc::bad_address);
  }
  ::socklen_t len = jewels::networking::SocketAddress::byte_size();
  if (::getsockname(*socket_, sockaddr->mutable_ptr(), &len) == -1)
  {
    return jewels::unexpected(static_cast<std::errc>(errno));
  }
  return *sockaddr;
}

} // namespace clockwork::pinion
