// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "clockwork/pinion/buffer_index.hh"
#include "clockwork/pinion/buffer_layout.hh"
#include "clockwork/pinion/tests/support/mock_slot.hh"
#include "jewels/std/span.hh"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>

namespace clockwork::pinion::support
{

/// Mockup of the underlying control block.
struct ControlBlock
{
  alignas(std::atomic_ref<BufferIndex>::required_alignment) BufferIndex head{};
  alignas(std::atomic_ref<BufferIndex>::required_alignment) BufferIndex tail{};
};

/// Underlying storage for a buffer.
template <BufferLayout layout>
struct BufferStorage
{
  std::array<SlotStorage<layout.message_size>, layout.num_slots> slots{};
  ControlBlock control_block{};
};

/// Check for padding after the buffer storage.
template <BufferLayout layout>
inline constexpr size_t storage_trail_padding =
  sizeof(BufferStorage<layout>) - (offsetof(BufferStorage<layout>, control_block) + sizeof(ControlBlock));

template <BufferLayout layout>
void zero_buffer_storage(BufferStorage<layout>& storage)
{
  std::ranges::fill(as_writable_bytes(jewels::as_single_item_span(storage)), std::byte{});
}

} // namespace clockwork::pinion::support
