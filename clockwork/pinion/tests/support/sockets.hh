// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "clockwork/io/network_var_packet.hh" // IWYU pragma: keep
#include "clockwork/io/var_packet.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "clockwork/pinion/tests/support/udp_payloads.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/std/span.hh"
#include "jewels/time/sync_time.hh"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <netinet/in.h> // IWYU pragma: keep
#include <span>
#include <string>

namespace clockwork::support
{

/// For a network socket, get the assigned addr information.  Useful
/// when providing port 0 and needing to know what port was assigned.
/// @param file_descriptor The fd of the socket.
/// @return The address information if successful.
jewels::expected<struct sockaddr_in, jewels::filesystem::ErrorCode> get_assigned_addr(int file_descriptor);

/// Send bytes to a socket.
/// @param data The bytes to send.
/// @param file_descriptor The fd of the socket to use for writing.
/// @param addr The address to send to.
jewels::expected<size_t, jewels::filesystem::ErrorCode>
send_to(std::span<const std::byte> data, int file_descriptor, struct sockaddr_in const& addr);

/// Helper class to bind to adn read from a socket.
class Receiver
{
public:
  /// Create a Receiver.  This binds to the socket if successful to allow a dynamic port.
  /// @param host Hostname to connect to.
  /// @param port Port to connect to (supports dynamic port 0).
  [[nodiscard]] static jewels::expected<Receiver, jewels::filesystem::ErrorCode>
  try_make(const std::string& host, uint16_t port);

  /// Read from the socket.
  /// @tparam Expected size of the packet.  Must match exactly.
  /// @return The payload if successful.
  template <size_t payload_size>
  [[nodiscard]] jewels::expected<std::array<std::byte, payload_size>, jewels::filesystem::ErrorCode> read();

  /// Get the port.  Useful when using a dynamically assigned port.
  [[nodiscard]] jewels::expected<uint16_t, jewels::filesystem::ErrorCode> port() const;

  /// Get the file_descriptor.  Useful for reusing the socket.
  [[nodiscard]] int fd();

private:
  /// Constructor called from try_make.
  explicit Receiver(jewels::filesystem::FileDescriptor&& file_descriptor);

  /// File descriptor.
  jewels::filesystem::FileDescriptor file_descriptor_;
};

/// Fill in a VarPacket.
/// @tparam payload_size The size of the payload in bytes.
/// @tparam Payload The payload type
/// @param message The packet to fill.
/// @param payload The payload to fill with.
template <size_t payload_size, class Payload>
void populate_value(Tachyon<io::VarPacket<payload_size>>& message, const Payload& payload)
{
  static_assert(sizeof(payload) <= payload_size);
  message.bytes.resize(sizeof(payload));
  std::ranges::copy(as_bytes(jewels::as_single_item_span(payload)), std::begin(message.bytes));
}

/// Specializtion for uint32_t.
void populate_value(Tachyon<JustAUInt32>& message, uint32_t payload);

/// Publish a message.
/// @tparam Msg The message type.
/// @tparam Payload The pyalod type.
/// @param handle The handle to publish to.
/// @param payload The payload to fill the message with.
template <class Msg, class Payload>
bool publish_to(pinion::PublisherHandle& handle, const Payload& payload)
{
  auto reserved_slot = handle.reserve();
  if (!reserved_slot)
  {
    return false;
  }
  auto publishable = pinion::Publishable<Msg>::try_make(jewels::memory::make_non_null_from_ref(*reserved_slot));
  if (!publishable)
  {
    return false;
  }

  auto& message = publishable->message();
  populate_value(message, payload);
  constexpr auto fake_publish_time{jewels::time::SyncTime{std::chrono::nanoseconds{12345}}};
  return static_cast<bool>(reserved_slot->commit(fake_publish_time));
}

} // namespace clockwork::support

#include "clockwork/pinion/tests/support/sockets.inl"
