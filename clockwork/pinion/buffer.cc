// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/buffer.hh"

#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/device_ptr.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/slot.hh"
#include "jewels/compiler/intrinsics.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <ranges>
#include <span>

namespace clockwork::pinion
{

__attribute__((weak)) DevicePtrFactory* get_buffer_dev_ptr_factory_impl()
{
  return nullptr;
}

namespace
{

jewels::expected<BufferIndex, BufferIndex>
increment_index(std::atomic_ref<BufferIndex> index, BufferIndex current, size_t count) noexcept
{
  const BufferIndex next{current + count};
  // This is very unlikely to ever happen with 64 bits.
  if (jewels::unlikely(next < current))
  {
    return jewels::unexpected{current};
  }

  // TODO(OI-698): update memory order
  if (!index.compare_exchange_strong(current, next))
  {
    // Current gets overwritten with the actual value of the atomic.
    return jewels::unexpected{current};
  }
  return next;
}

} // namespace

namespace detail
{

uint64_t to_position(BufferIndex index, BufferLayout layout) noexcept
{
  return index % layout.num_slots;
}

} // namespace detail

BufferIterator::BufferIterator(
  AlignedBytePtr<Slot::slot_alignment> buffer, BufferLayout layout, BufferIndex index) noexcept
  : buffer_{buffer}, layout_{layout}, index_{index}
{
}

Slot BufferIterator::dereference() const noexcept
{
  return Slot{
    buffer_ + static_cast<std::ptrdiff_t>(detail::to_position(index_, layout_) * slot_stride(layout_)),
    layout_.message_size,
    get_buffer_dev_ptr_factory_impl()};
}

bool BufferIterator::equal(const BufferIterator& other) const noexcept
{
  return index_ == other.index_;
}

BufferIterator& BufferIterator::increment() noexcept
{
  ++index_;
  return *this;
}

BufferIterator& BufferIterator::decrement() noexcept
{
  --index_;
  return *this;
}

BufferIterator& BufferIterator::advance(std::ptrdiff_t n) noexcept
{
  index_ += static_cast<BufferIndex>(n);
  return *this;
}

std::ptrdiff_t BufferIterator::distance_to(const BufferIterator& other) const noexcept
{
  return static_cast<std::ptrdiff_t>(other.index_ - index_);
}

BufferIndex BufferIterator::index() const noexcept
{
  return index_;
}

[[nodiscard]] Slot BufferIterator::operator[](size_t index) const noexcept
{
  auto copy = *this;
  copy.advance(static_cast<std::ptrdiff_t>(index)); // NOLINT(cert-err33-c) False positive
  return *copy;
}

bool is_sentinel_iterator(const BufferIterator& iterator) noexcept
{
  return static_cast<const void*>(iterator.buffer_.get()) == static_cast<const void*>(&BufferIterator::default_data_);
}

jewels::expected<Buffer, InitError> Buffer::try_make(std::span<std::byte> bytes, const BufferLayout& layout) noexcept
{
  if (buffer_size(layout) != bytes.size())
  {
    return jewels::unexpected{InitError::invalid_size};
  }

  const auto maybe_aligned_ptr =
    AlignedBytePtr<Slot::slot_alignment>::try_make(jewels::memory::ObjectPtr<std::byte>{bytes.data()});
  if (!maybe_aligned_ptr)
  {
    return jewels::unexpected{InitError::invalid_alignment};
  }

  return Buffer{*maybe_aligned_ptr, layout};
}

Buffer::Buffer(AlignedBytePtr<Slot::slot_alignment> buffer, BufferLayout layout) noexcept
  : buffer_{buffer}, layout_{layout}
{
}

BufferIndex Buffer::head() const noexcept
{
  // TODO(OI-698): update memory order
  return head_ref().load();
}

jewels::expected<BufferIndex, BufferIndex> Buffer::increment_head(BufferIndex current, size_t count) noexcept
{
  // If published once, only allow a single increment at initialization
  if (is_published_once() && (current > 0 || count > 1))
  {
    return jewels::unexpected{head()};
  }
  return increment_index(head_ref(), current, count);
}

BufferIterator Buffer::begin() const noexcept
{
  return BufferIterator{buffer_, layout_, tail()};
}

BufferIndex Buffer::tail() const noexcept
{
  // TODO(OI-698): update memory order
  return tail_ref().load();
}

jewels::expected<BufferIndex, BufferIndex> Buffer::increment_tail(BufferIndex current, size_t count) noexcept
{
  // If this is only for a single publish, the tail should never update since the tail should only move once the buffer
  // is full.
  if (is_published_once() && (current > 0 || count > 0))
  {
    return jewels::unexpected{tail()};
  }
  return increment_index(tail_ref(), current, count);
}

BufferIterator Buffer::end() const noexcept
{
  return BufferIterator{buffer_, layout_, head()};
}

AlignedBytePtr<Slot::slot_alignment> Buffer::get() const noexcept
{
  return buffer_;
}

std::span<std::byte> Buffer::bytes() const noexcept
{
  return std::span{buffer_.get(), buffer_size(layout_)};
}

const BufferLayout& Buffer::layout() const noexcept
{
  return layout_;
}

std::atomic_ref<BufferIndex> Buffer::head_ref() const noexcept
{
  const auto head_span = std::span<std::byte, sizeof(BufferIndex)>{
    std::next(buffer_.get(), static_cast<std::ptrdiff_t>(head_offset(layout_))), sizeof(BufferIndex)};
  return std::atomic_ref<BufferIndex>{*detail::marshal_as<BufferIndex>(head_span)};
}

std::atomic_ref<BufferIndex> Buffer::tail_ref() const noexcept
{
  const auto tail_span = std::span<std::byte, sizeof(BufferIndex)>{
    std::next(buffer_.get(), static_cast<std::ptrdiff_t>(tail_offset(layout_))), sizeof(BufferIndex)};
  return std::atomic_ref<BufferIndex>{*detail::marshal_as<BufferIndex>(tail_span)};
}

bool Buffer::is_published_once() const noexcept
{
  return layout_.is_published_once;
}

size_t Buffer::get_publish_count() const noexcept
{
  return static_cast<size_t>(head());
}

[[nodiscard]] bool Buffer::still_available(const BufferIterator& iterator) const
{
  if (is_sentinel_iterator(iterator))
  {
    return false;
  }
  return std::begin(*this) <= iterator && std::end(*this) > iterator;
}

jewels::expected<BufferIndex, ReserveError> Buffer::reserve(size_t count) noexcept
{
  if (reserved_ > 0U)
  {
    return jewels::unexpected{ReserveError::existing_reservation};
  }
  if (count > layout().num_slots)
  {
    return jewels::unexpected{ReserveError::count_too_large};
  }
  const auto current_tail = tail();
  const auto current_head = head();
  const auto slots_in_use = current_head - current_tail;
  const auto slots_available = layout().num_slots - slots_in_use;
  const auto slots_to_hide = count > slots_available ? count - slots_available : 0UL;

  // Increment the tail to hide enough elements to satisfy the reservation.
  const auto maybe_new_tail = increment_tail(current_tail, slots_to_hide);
  if (!maybe_new_tail)
  {
    return jewels::unexpected{ReserveError::unexpected_tail};
  }

  // The control block has been updated already so subscribers can't
  // see these slots anymore.  Safe to zero out headers and footers of
  // hidden slots.
  for (auto hidden_slot : std::ranges::subrange<BufferIterator>{
         BufferIterator{get(), layout(), current_tail}, BufferIterator{get(), layout(), *maybe_new_tail}})
  {
    for (auto bytes : hidden_slot.headers_footers())
    {
      if (!bytes.empty())
      {
        std::memset(bytes.data(), 0, bytes.size_bytes());
      }
    }
  }

  reserved_ = count;
  return current_head;
}

jewels::expected<void, WriteError> Buffer::commit(BufferIndex reserved_slot) noexcept
{
  return commit(reserved_slot, reserved_);
}

jewels::expected<void, WriteError> Buffer::commit(BufferIndex reserved_slot, size_t actual_count) noexcept
{
  const auto current_head = head();
  if (reserved_ == 0UL || current_head != reserved_slot || actual_count > reserved_)
  {
    return jewels::unexpected{WriteError::unexpected_reservation};
  }

  // Incrementing the head by actual_count exposes only that many slots.
  const auto maybe_new_head = increment_head(current_head, actual_count);
  if (!maybe_new_head)
  {
    return jewels::unexpected{WriteError::unexpected_head};
  }
  reserved_ = 0UL;
  return {};
}

jewels::expected<void, WriteError> Buffer::discard(BufferIndex reserved_slot) noexcept
{
  if (reserved_ == 0UL || head() != reserved_slot)
  {
    return jewels::unexpected{WriteError::unexpected_reservation};
  }
  reserved_ = 0UL;
  return {};
}

} // namespace clockwork::pinion
