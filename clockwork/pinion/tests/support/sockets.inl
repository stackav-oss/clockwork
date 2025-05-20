// IWYU pragma: private, include "clockwork/pinion/tests/support/sockets.hh"
#pragma once

#include "clockwork/pinion/tests/support/sockets.hh"

#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/std/expected.hh"

#include <array>
#include <cerrno>
#include <cstddef>
#include <netinet/in.h> // IWYU pragma: keep
#include <sys/socket.h>

namespace clockwork::support
{

template <size_t payload_size>
jewels::expected<std::array<std::byte, payload_size>, jewels::filesystem::ErrorCode> Receiver::read()
{
  std::array<std::byte, payload_size> bytes{};
  const auto bytes_received =
    ::recvfrom(*file_descriptor_, static_cast<void*>(bytes.data()), bytes.size(), MSG_TRUNC, nullptr, nullptr);
  if (bytes_received < 0)
  {
    return jewels::unexpected{jewels::filesystem::make_error_code(errno)};
  }
  if (bytes_received != bytes.size())
  {
    return jewels::unexpected{jewels::filesystem::make_error_code(EMSGSIZE)};
  }
  return bytes;
}

[[nodiscard]] int Receiver::fd()
{
  return *file_descriptor_;
}

} // namespace clockwork::support
