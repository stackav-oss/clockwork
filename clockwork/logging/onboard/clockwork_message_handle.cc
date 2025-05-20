// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/onboard/clockwork_message_handle.hh"

namespace clockwork_logging::onboard
{

ClockworkMessageHandle::ClockworkMessageHandle(
  jewels::memory::ObjectPtr<const clockwork::pinion::Buffer> buffer_ptr,
  const clockwork::pinion::BufferIterator& buffer_iterator)
  : subscriber_handle_(buffer_ptr), buffer_iterator_(buffer_iterator)
{
}

[[nodiscard]] bool ClockworkMessageHandle::is_valid() const noexcept
{
  return subscriber_handle_.still_available(buffer_iterator_);
}

[[nodiscard]] const clockwork::pinion::BufferIterator& ClockworkMessageHandle::get_buffer_iterator() const noexcept
{
  return buffer_iterator_;
}

} // namespace clockwork_logging::onboard
