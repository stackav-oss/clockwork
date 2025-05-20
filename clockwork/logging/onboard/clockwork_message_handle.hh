// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "jewels/memory/pointers.hh"

namespace clockwork_logging::onboard
{

/// Handle for a message stored in a clockwork pinion buffer
class ClockworkMessageHandle
{
public:
  /// Constructor
  /// @param[in] buffer_ptr Pinion buffer pointer
  /// @param[in] buffer_iterator Pinion buffer iterator
  ClockworkMessageHandle(
    jewels::memory::ObjectPtr<const clockwork::pinion::Buffer> buffer_ptr,
    const clockwork::pinion::BufferIterator& buffer_iterator);

  ~ClockworkMessageHandle() = default;

  ClockworkMessageHandle(const ClockworkMessageHandle&) = default;
  ClockworkMessageHandle& operator=(const ClockworkMessageHandle&) = default;
  ClockworkMessageHandle(ClockworkMessageHandle&&) = default;
  ClockworkMessageHandle& operator=(ClockworkMessageHandle&&) = default;

  /// Test whether the message buffer is valid
  /// @return True if the buffer slot for the message is still available
  [[nodiscard]] bool is_valid() const noexcept;

  /// Buffer iterator accessor
  [[nodiscard]] const clockwork::pinion::BufferIterator& get_buffer_iterator() const noexcept;

private:
  /// Pinion subscriber handle
  clockwork::pinion::SubscriberHandle subscriber_handle_;

  /// Pinion buffer iterator
  clockwork::pinion::BufferIterator buffer_iterator_;
};

} // namespace clockwork_logging::onboard
