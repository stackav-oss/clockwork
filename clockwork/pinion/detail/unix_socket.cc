// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/detail/unix_socket.hh"

#include <__stddef_offsetof.h>

#include <cerrno>
#include <sys/socket.h>
#include <sys/un.h>
#include <type_traits>
#include <utility>

namespace clockwork::pinion
{
namespace
{

struct Address
{
  static jewels::expected<Address, std::errc> create_abstract(std::string_view name)
  {
    // The address is created in the abstract space, which means it has an initial null and uses an explicit size rather
    // than a terminator. Abstract sockets are a Linux extension so end up with fewer portability concerns and more
    // assumptions hardcoded in the APIs, though this tries to minimize assumptions as much as is reasonable.
    constexpr size_t max_length = sizeof(std::declval<struct sockaddr_un>().sun_path) - 1;
    Address addr{};
    if (name.length() > max_length)
    {
      return jewels::unexpected(std::errc::filename_too_long);
    }
    addr.address_.sun_family = AF_UNIX;
    // Set the first byte to 0 to indicate abstract namespace
    addr.address_.sun_path[0] = 0;
    // Place the name starting at the second byte
    char* const name_dest = &addr.address_.sun_path[1];
    addr.length_ = static_cast<socklen_t>(name.copy(name_dest, max_length));
    addr.length_ += offsetof(struct sockaddr_un, sun_path) + 1;
    return addr;
  }

  [[nodiscard]] const struct sockaddr* address() const noexcept
  {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) Required by C syscall interface
    return reinterpret_cast<const struct sockaddr*>(&address_);
  }
  [[nodiscard]] socklen_t length() const noexcept
  {
    return length_;
  }

  struct sockaddr_un address_;
  socklen_t length_;
};

} // namespace

jewels::expected<UnixSocket, std::errc> UnixSocket::create_bind(std::string_view name)
{
  auto address = Address::create_abstract(name);
  if (!address)
  {
    return jewels::unexpected(address.error());
  }
  jewels::filesystem::FileDescriptor sock{::socket(AF_UNIX, SOCK_SEQPACKET, 0)};
  if (!sock)
  {
    return jewels::unexpected(static_cast<std::errc>(errno));
  }
  if (::bind(*sock, address->address(), address->length()) == -1)
  {
    return jewels::unexpected(static_cast<std::errc>(errno));
  }
  return UnixSocket(std::move(sock));
}

jewels::expected<UnixSocket, std::errc> UnixSocket::create_connect(std::string_view name)
{
  auto address = Address::create_abstract(name);
  if (!address)
  {
    return jewels::unexpected(address.error());
  }
  jewels::filesystem::FileDescriptor sock{::socket(AF_UNIX, SOCK_SEQPACKET, 0)};
  if (!sock)
  {
    return jewels::unexpected(static_cast<std::errc>(errno));
  }
  if (::connect(*sock, address->address(), address->length()) == -1)
  {
    return jewels::unexpected(static_cast<std::errc>(errno));
  }
  return UnixSocket(std::move(sock));
}

UnixSocket::UnixSocket(jewels::filesystem::FileDescriptor sock) noexcept
  : socket_(std::move(sock))
{
}

jewels::expected<void, std::errc> UnixSocket::listen(int backlog)
{
  if (::listen(*socket_, backlog) == -1)
  {
    return jewels::unexpected(static_cast<std::errc>(errno));
  }
  return {};
}

int UnixSocket::descriptor() const noexcept
{
  return *socket_;
}

void UnixSocket::close()
{
  socket_.forced_close();
}

} // namespace clockwork::pinion
