// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"

#include <array>
#include <cstddef>
#include <cstdlib> // IWYU pragma: keep

namespace clockwork
{

///
/// Throw if return is unexpected
///
template <class T, class... Args>
auto unwrap_try_make(Args&&... args);

///
/// Helper class to make in memory channels easier to instantiate.
///
template <typename MsgType, size_t num_slots, bool is_published_once>
class InMemoryChannel
{
public:
  explicit InMemoryChannel(jewels::memory::MemoryResource resource);
  ~InMemoryChannel() = default;
  InMemoryChannel(const InMemoryChannel&) = delete;
  InMemoryChannel& operator=(const InMemoryChannel&) = delete;
  InMemoryChannel(InMemoryChannel&&) = default;
  InMemoryChannel& operator=(InMemoryChannel&&) = default;

  [[nodiscard]] pinion::PublisherHandle make_publisher(size_t num_observers);
  [[nodiscard]] pinion::SubscriberHandle make_subscriber();
  [[nodiscard]] pinion::Buffer& buffer();
  [[nodiscard]] const pinion::Buffer& buffer() const;

private:
  static constexpr pinion::BufferLayout layout{
    .num_slots = num_slots,
    .message_size = sizeof(MsgType),
    .is_published_once = is_published_once,
  };

  struct Storage
  {
    alignas(pinion::Slot::slot_alignment) std::array<std::byte, buffer_size(layout)> bytes;
  };

  jewels::memory::pmr_unique_ptr<Storage> storage_;
  pinion::Buffer buffer_;
  jewels::memory::MemoryResource resource_;
};

} // namespace clockwork

#include "clockwork/pinion/in_memory_channel.inl"
