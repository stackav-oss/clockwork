// IWYU pragma: private, include "clockwork/pinion/buffer_layout.hh"
#pragma once

#include "clockwork/pinion/buffer_layout.hh"

#include "clockwork/pinion/slot.hh"

#include <cstddef>

namespace clockwork::pinion
{

constexpr size_t slot_stride(const BufferLayout& layout) noexcept
{
  return slot_size(layout.message_size) / Slot::slot_alignment;
}

constexpr size_t buffer_size(const BufferLayout& layout) noexcept
{
  return (slot_size(layout.message_size) * layout.num_slots) + BufferLayout::control_block_size;
}

constexpr size_t head_offset(const BufferLayout& layout) noexcept
{
  // This should already be aligned.
  return slot_size(layout.message_size) * layout.num_slots;
}

constexpr size_t tail_offset(const BufferLayout& layout) noexcept
{
  // This should already be aligned.
  return (slot_size(layout.message_size) * layout.num_slots) + BufferLayout::head_size;
}

} // namespace clockwork::pinion
