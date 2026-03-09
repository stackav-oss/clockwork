// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/pinion/aligned_pointer.hh"
#include "clockwork/pinion/buffer_index.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/slot.hh"
#include "jewels/math/power_of_two.hh"
#include "jewels/std/expected.hh"

#include <boost/iterator/iterator_facade.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>

namespace clockwork::pinion
{

/// Describes the layout of the buffer.
struct BufferLayout
{
  /// Size of the head index in bytes.
  static constexpr auto head_size{sizeof(BufferIndex)};
  /// Size of the tail index in bytes.
  static constexpr auto tail_size{sizeof(BufferIndex)};
  /// Size of the entire control block that trails the last slot.
  static constexpr auto control_block_size{head_size + tail_size};

  /// Alignment of the head index.
  static constexpr auto head_alignment{std::atomic_ref<BufferIndex>::required_alignment};
  /// Alignment of the tail index.
  static constexpr auto tail_alignment{std::atomic_ref<BufferIndex>::required_alignment};

  /// Number of slots in the buffer.
  size_t num_slots;
  /// Size of the message payload.
  size_t message_size;
  /// True if the channel is only published once
  bool is_published_once;
};

/// Get the size of the buffer as a multiple of the alignment.
/// @param layout Describes the layout of the buffer.
constexpr size_t slot_stride(const BufferLayout& layout) noexcept;

/// Get the size of the buffer in total bytes.
/// @param layout Describes the layout of the buffer.
constexpr size_t buffer_size(const BufferLayout& layout) noexcept;

/// Get the offset to the head index in the control block.
/// @param layout Describes the layout of the buffer.
constexpr size_t head_offset(const BufferLayout& layout) noexcept;

/// Get the offset to the tail index in the control block.
/// @param layout Describes the layout of the buffer.
constexpr size_t tail_offset(const BufferLayout& layout) noexcept;

/// Iterator over the slots in a buffer.
class BufferIterator : public boost::iterator_facade<BufferIterator, Slot, std::random_access_iterator_tag, Slot>
{
  /// An aligned struct used to allow the iterator to be default constructible.
  static inline struct alignas(Slot::slot_alignment) DefaultData
  {
    // Allows a default constructor for the iterator.
  } default_data_; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables) Private to class

public:
  /// Default constructor.
  /// @note Required for all iterator concepts.
  BufferIterator() = default;

  /// Construct a buffer iterator.
  /// @param buffer A pointer to the start of the underlying buffer.
  /// @param layout The layout of the buffer.
  /// @param index The index of the element for the iterator position.
  BufferIterator(AlignedBytePtr<Slot::slot_alignment> buffer, BufferLayout layout, BufferIndex index) noexcept;

  /// Dereference the iterator to get the current slot.
  /// @note Dereferencing requires a modulo operation which is not
  /// cheap.  Best practice is to dereference once and store a
  /// temporary instead of dereferencing multiple times.

  /// @param A reference to the underlying object.
  [[nodiscard]] Slot dereference() const noexcept;

  /// Check equality of two iterators.
  /// @param other The other iterator.
  /// @return True if equal and false otherwise.
  [[nodiscard]] bool equal(const BufferIterator& other) const noexcept;

  /// Increment one position.
  /// @return A reference to the current iterator after incrementing.
  BufferIterator& increment() noexcept;

  /// Decrement one position.
  /// @return A reference to the current iterator after decrementing.
  BufferIterator& decrement() noexcept;

  /// Advance the iterator n positions.
  /// @param n The number of positions to advance (can be positive or negative).
  /// @return A reference to the iterator after advancing.
  BufferIterator& advance(std::ptrdiff_t n) noexcept;

  /// Override the bracket operator from the boost iterator facade.
  /// This is required to satisfy range concepts.  Otherwise boost
  /// returns a proxy reference type.
  /// @return The accessed Slot based on the current position and the index.
  [[nodiscard]] Slot operator[](size_t index) const noexcept;

  /// Distance to another iterator.
  /// @param other The other iterator.
  /// @return The number of times to increment (or decrement if negative) to get to other.
  [[nodiscard]] std::ptrdiff_t distance_to(const BufferIterator& other) const noexcept;

  /// Access the iterator index
  /// @return Iterator index, sentinal iterators have index 0
  [[nodiscard]] BufferIndex index() const noexcept;

private:
  friend bool is_sentinel_iterator(const BufferIterator& iterator) noexcept;

  /// Pointer to the start of the underlying buffer.
  AlignedBytePtr<Slot::slot_alignment> buffer_{AlignedBytePtr<Slot::slot_alignment>::from_ref(default_data_)};
  /// Layout of the buffer.
  BufferLayout layout_{};
  /// Modulo the number of slots gives the index to the slot within the buffer.
  BufferIndex index_{};
};

/// Check if an iterator is the default constructed iterator.  This
/// can be used as a sentinel value to indicate it was not
/// initialized.
/// @note Cogs will hold an iterator to the last consumed message.  At
/// startup, when nothing no messages have been read, the cog can
/// leave the iterator default constructed instead of having to wrap
/// it in an optional.
/// @param iterator The iterator to check.
/// @return True if the sentinel iterator and false otherwise.
bool is_sentinel_iterator(const BufferIterator& iterator) noexcept;

/// An abstraction over an underlying comms buffer providing access to the data.
class Buffer
{
public:
  /// Construct a buffer from an existing memory region.  Could be a
  /// buffer allocated in shmem or just a buffer on the heap.
  /// @note Can fail if the provided buffer is not the right size or not aligned properly.
  /// @param bytes A preallocated byte buffer.
  /// @param layout Sizes / alignments / offset for the layout of the buffer.
  /// @return A constructed Buffer object or an error.
  [[nodiscard]] static jewels::expected<Buffer, InitError>
  try_make(std::span<std::byte> bytes, const BufferLayout& layout) noexcept;

  /// Atomically get the head index. The head is one past the newest slot
  /// in the buffer. The result modulo num_slots is the position in
  /// the buffer.
  [[nodiscard]] BufferIndex head() const noexcept;

  /// Atomically increment the head.
  /// @param current Expected current head value.
  /// @return The new value or an error.
  [[nodiscard]] jewels::expected<BufferIndex, BufferIndex> increment_head(BufferIndex current, size_t count) noexcept;

  /// Obtain an iterator pointing to the oldest slot in the buffer.
  /// @return The iterator.
  [[nodiscard]] BufferIterator begin() const noexcept;

  /// Atomically get the tail index. This is the oldest slot in the
  /// buffer. The result modulo num_slots is the position in the
  /// buffer.
  [[nodiscard]] BufferIndex tail() const noexcept;

  /// Atomically increment the tail.
  /// @param current Expected current tail value.
  /// @return The new value or an error.
  [[nodiscard]] jewels::expected<BufferIndex, BufferIndex> increment_tail(BufferIndex current, size_t count) noexcept;

  /// Obtain an iterator pointing to one past the newest slot in the buffer.
  /// @return The iterator.
  [[nodiscard]] BufferIterator end() const noexcept;

  /// Underlying buffer as bytes.
  [[nodiscard]] std::span<std::byte> bytes() const noexcept;

  /// Aligned pointer to the underlying buffer.
  [[nodiscard]] AlignedBytePtr<Slot::slot_alignment> get() const noexcept;

  /// Layout of the buffer.
  [[nodiscard]] const BufferLayout& layout() const noexcept;

  /// Check whether this buffer is published once.
  [[nodiscard]] bool is_published_once() const noexcept;

  /// Get the number of messages that have been published to the buffer
  [[nodiscard]] size_t get_publish_count() const noexcept;

  /// Check if the iterator still points to an available message.
  /// This is useful for subscribers to check if their messages was
  /// overwritten while reading it.  After execution, if this returns
  /// true for the oldest message they consumed, then the data was not
  /// corrupted.
  /// @param iterator An iterator to a slot in the buffer to check
  /// availability for.
  /// @return True if still available and false otherwise.
  [[nodiscard]] bool still_available(const BufferIterator& iterator) const;

private:
  /// Get an atomic reference to the head index.
  [[nodiscard]] std::atomic_ref<BufferIndex> head_ref() const noexcept;

  /// Get an atomic reference to the tail index.
  [[nodiscard]] std::atomic_ref<BufferIndex> tail_ref() const noexcept;

  // Constructor to be called by the factory function.
  Buffer(AlignedBytePtr<Slot::slot_alignment> buffer, BufferLayout layout) noexcept;

  /// Span of bytes for the underlying buffer.
  AlignedBytePtr<Slot::slot_alignment> buffer_;

  /// Layout of the buffer.
  BufferLayout layout_;
};

namespace detail
{

/// Convert a buffer index to a slot position within the buffer.
/// @param index The buffer index.
/// @param layout The buffer layout.
/// @return The slot position.
uint64_t to_position(BufferIndex index, BufferLayout layout) noexcept;

} // namespace detail

} // namespace clockwork::pinion

#include "clockwork/pinion/buffer.inl"
