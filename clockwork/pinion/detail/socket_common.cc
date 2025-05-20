// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/detail/socket_common.hh"

#include <fcntl.h>

namespace clockwork::pinion
{

bool set_nonblocking(int socket_fd, bool value) noexcept
{
  constexpr auto o_nonblock_u = static_cast<unsigned int>(O_NONBLOCK);
  const int flags = ::fcntl(socket_fd, F_SETFL, 0);
  if (flags != -1)
  {
    auto uflags = static_cast<unsigned int>(flags);
    if (value && (uflags & o_nonblock_u) != o_nonblock_u)
    {
      uflags |= o_nonblock_u;
    }
    else if (!value && (uflags & o_nonblock_u) != 0)
    {
      uflags &= ~o_nonblock_u;
    }
    else
    {
      return true;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) Required by C syscall
    if (::fcntl(socket_fd, F_SETFL, static_cast<int>(uflags)) != -1)
    {
      return true;
    }
  }
  return false;
}

} // namespace clockwork::pinion
