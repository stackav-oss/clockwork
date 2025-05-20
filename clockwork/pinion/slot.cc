// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/slot.hh"

#include "clockwork/memory/start_lifetime_as.hh"

namespace clockwork::pinion
{

Slot::Slot(AlignedPtr<slot_alignment> ptr, size_t message_size) noexcept
  : bytes_{std::span<std::byte>{ptr.get(), slot_size(message_size)}}, message_size_{message_size}
{
}

jewels::memory::ObjectPtr<const Header> Slot::header() const noexcept
{
  return detail::marshal_as<const Header>(bytes_.subspan<header_offset, sizeof(Header)>());
}
jewels::memory::ObjectPtr<Header> Slot::header() noexcept
{
  return detail::marshal_as<Header>(bytes_.subspan<header_offset, sizeof(Header)>());
}

std::span<const std::byte> Slot::message() const noexcept
{
  return bytes_.subspan(message_offset, message_size_);
}
std::span<std::byte> Slot::message() noexcept
{
  return bytes_.subspan(message_offset, message_size_);
}

std::span<const std::byte> Slot::bytes() const noexcept
{
  return bytes_;
}

std::span<std::byte> Slot::bytes() noexcept
{
  return bytes_;
}

std::array<std::span<std::byte>, 2UL> Slot::headers_footers() noexcept
{
  auto all_bytes = bytes();
  // Everything before the message.
  auto headers = all_bytes.subspan(0, message_offset);
  // Everything after the message.
  auto footers = all_bytes.subspan(message_offset + message_size_);
  return std::array{headers, footers};
}

} // namespace clockwork::pinion
