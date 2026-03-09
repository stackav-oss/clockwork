// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/buffer_index.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/observer.hh"
#include "clockwork/pinion/slot.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"

#include <wise_enum.h>

#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <optional>
#include <ranges>
#include <span>
#include <vector>

namespace clockwork::pinion
{

/// Forward declaration for a reserved slot.
class ReservedSlot;
/// Forward declaration for a batch reserved slot.
class BatchReservedSlot;

/// Wrapper around the Buffer interface used for publishers to write messages.
class PublisherHandle
{
public:
  /// Construct a buffer from an existing memory region.  Could be a
  /// buffer allocated by the bridge or just a buffer on the heap.
  /// @note Uses a memory resource to allocate space for the observer storage.
  /// @note This does not take ownership of the underlying buffer.
  /// @param buffer The underlying buffer handle.
  /// @param num_observers The number of observers to reserve space for.
  /// @param resource A memory resource.
  PublisherHandle(
    jewels::memory::ObjectPtr<Buffer> buffer, size_t num_observers, jewels::memory::MemoryResource resource) noexcept;

  ~PublisherHandle();
  PublisherHandle(PublisherHandle&& other) = default;
  PublisherHandle(const PublisherHandle&) = delete;
  void operator=(const PublisherHandle&) = delete;
  void operator=(PublisherHandle&&) = delete;

  /// Get the layout of the underlying buffer.
  [[nodiscard]] const BufferLayout& layout() const noexcept;

  /// Get the underlying buffer
  [[nodiscard]] const Buffer& buffer() const noexcept;

  /// Add an observer (e.g., a subscriber) to be notified whenever a
  /// new message is committed.
  /// @note This does not take ownership of the observer.
  /// @note Can only add observers if there is reserved space.  If
  /// full, an observer will not be added.
  /// @param observer The observer to notify.
  /// @return True if added and false otherwise.
  [[nodiscard]] bool add_observer(jewels::memory::ObjectPtr<Observer> observer) noexcept;

  /// Reserve space for the next message.
  /// @note There should only ever be one publisher calling this
  /// method.
  /// @note After calling reserve, either commit or discard must be
  /// called before calling reserve again.
  [[nodiscard]] jewels::expected<ReservedSlot, ReserveError> reserve(bool connected = true) noexcept;

  [[nodiscard]] jewels::expected<BatchReservedSlot, ReserveError> reserve(size_t count) noexcept;

private:
  /// Allow ReservedSlot to call `commit` and `discard`.
  friend class ReservedSlot;
  friend class BatchReservedSlot;

  /// Commit a reserved slot with a specific sequence number and source commit time
  /// @param reserved_slot A previously reserved slot.
  /// @param publish_time Message publish timestamp
  /// @param sequence_number Overrides for the message sequence number
  /// @param source_commit_time Message commit time from the original sender
  [[nodiscard]] jewels::expected<void, WriteError> commit(
    BufferIndex reserved_slot,
    jewels::time::SyncTime publish_time,
    uint64_t sequence_number,
    jewels::time::SyncTime source_commit_time) noexcept;

  /// Commit a reserved slot or reserved batch
  /// @param reserved_slot A previously reserved slot.
  /// @param publish_time Message publish timestamp
  [[nodiscard]] jewels::expected<void, WriteError>
  commit(BufferIndex reserved_slot, jewels::time::SyncTime publish_time) noexcept;

  /// Common commit processing
  /// @param publish_time Message publish timestamp
  [[nodiscard]] jewels::expected<void, WriteError> commit_common(jewels::time::SyncTime publish_time) noexcept;

  /// Discard a reserved slot.
  /// @param reserved_slot A previously reserved slot.
  [[nodiscard]] jewels::expected<void, WriteError> discard(BufferIndex reserved_slot) noexcept;

  /// Hide slots from subscribers.  This is used to reserve a slot for publishers to write to.
  /// @pre count must be less than the number of slots.
  /// @param count The number of slots to hide.
  /// @return An error code on failure.
  [[nodiscard]] jewels::expected<void, ReserveError> hide(size_t count) noexcept;

  /// The underlying comms buffer.
  jewels::memory::ObjectPtr<Buffer> buffer_;

  /// All observers of the channel.
  std::pmr::vector<jewels::memory::ObjectPtr<Observer>> observers_;

  /// Index to the head.
  BufferIndex head_;

  /// Index to the tail.
  BufferIndex tail_;

  /// Whether or not a slot is currently reserved.
  size_t reserved_{0UL};
};

/// A wrapper for an enum to represent the state of a reserved slot.
/// @note The main reason to have this wrapper is to implement the
/// move-construction behavior once and let any reserved slot type use
/// a default move-constructor to avoid bugs with new fields.
class ReservationState
{
public:
  /// State of the reservation.
  WISE_ENUM_CLASS_MEMBER((State, uint8_t), discard, commit, ignore)

  ReservationState() noexcept = default;

  /// The move constructor handles setting the moved-from state to ignore.
  ReservationState(ReservationState&& other) noexcept;

  ReservationState(const ReservationState&) noexcept = delete;
  ReservationState& operator=(const ReservationState&) noexcept = delete;
  ReservationState& operator=(ReservationState&&) noexcept = delete;

  ~ReservationState() noexcept = default;

  /// Set the reservation state.
  void set(State state) noexcept;

  /// Get the reservation state.
  [[nodiscard]] State get() const noexcept;

  /// Equality operator.
  [[nodiscard]] bool operator==(const ReservationState&) const noexcept = default;

  /// Equality operator overload for the underlying enum.
  [[nodiscard]] bool operator==(State state) const noexcept;

private:
  /// The underlying state.
  State state_{State::discard};
};

/// A wrapper to guarantee that any reserved slot is either committed or discarded.
class ReservedSlot
{
public:
  /// Move constructor.
  /// @param other The slot to construct from.
  ReservedSlot(ReservedSlot&& other) noexcept = default;

  ReservedSlot(const ReservedSlot&) = delete;
  void operator=(const ReservedSlot&) = delete;
  void operator=(ReservedSlot&&) = delete;

  /// Logs console message if marked and not processed.
  ~ReservedSlot() noexcept(false);

  /// Signal that the reserved slot should be committed when the reserved slot destructs.
  void mark_for_commit() noexcept;

  /// Signal that the reserved slot should be committed when the reserved slot destructs.
  /// @param publish_time The time to use for the publish timestamp.
  void sim_only_mark_for_commit_with_fake_timestamp(jewels::time::SyncTime publish_time) noexcept;

  /// Signal that the reserved slot should be discarded when the reserved slot destructs.
  void mark_for_discard() noexcept;

  /// Commit or discard the message if marked.
  /// @param publish_time Used if committing to specify time of commit.
  [[nodiscard]] jewels::expected<void, WriteError> process(jewels::time::SyncTime publish_time) noexcept;

  /// Mark for commit and process in one call overriding the message sequence number.
  /// @param publish_time Used to specify time of commit.
  /// @param sequence_number Override for the message sequence number
  /// @param source_commit_time Message commit time from the original sender
  [[nodiscard]] jewels::expected<void, WriteError> commit(
    jewels::time::SyncTime publish_time, uint64_t sequence_number, jewels::time::SyncTime source_commit_time) noexcept;

  /// Mark for commit and process in one call.
  /// @param publish_time Used to specify time of commit.
  [[nodiscard]] jewels::expected<void, WriteError> commit(jewels::time::SyncTime publish_time) noexcept;

  /// Mark for discard and process in one call.
  [[nodiscard]] jewels::expected<void, WriteError> discard() noexcept;

  /// Get the current reservation state.
  [[nodiscard]] ReservationState::State state() const noexcept;

  /// Access the reserved slot.
  /// @note Will throw if the underlying value has been moved from.
  [[nodiscard]] Slot slot() const noexcept;

  /// Check if the reserved slot is connected to a channel.
  [[nodiscard]] bool connected() const noexcept;

private:
  friend class PublisherHandle;

  /// Construct a reserved slot.
  /// @param publisher_handle A pointer to the publisher handle that reserved the slot.
  /// @param reserved_index Index to the reserved slot.
  ReservedSlot(
    jewels::memory::ObjectPtr<PublisherHandle> publisher_handle, BufferIndex reserved_index, bool connected) noexcept;

  /// Handle to commit / discard the slot.
  jewels::memory::ObjectPtr<PublisherHandle> publisher_handle_;

  /// Index to the reserved slot.
  BufferIndex reserved_index_;

  /// Whether or not to commit.
  ReservationState state_{};

  /// Optional publish time to use that overrides the time passed to process/commit.
  std::optional<jewels::time::SyncTime> publish_time_{};

  /// Whether or not the slot is connected to a channel.
  bool connected_;
};

/// A wrapper to guarantee that any reserved slot is either committed or discarded.
class BatchReservedSlot
{
public:
  /// Move constructor.
  /// @param other The slot to construct from.
  BatchReservedSlot(BatchReservedSlot&& other) noexcept = default;

  BatchReservedSlot(const BatchReservedSlot&) = delete;
  void operator=(const BatchReservedSlot&) = delete;
  void operator=(BatchReservedSlot&&) = delete;

  /// Logs console message if marked and not processed.
  ~BatchReservedSlot() noexcept(false);

  /// Mark for commit and process in one call.
  /// @param publish_time Used to specify time of commit.
  [[nodiscard]] jewels::expected<void, WriteError> commit(jewels::time::SyncTime publish_time) noexcept;

  /// Mark for discard and process in one call.
  [[nodiscard]] jewels::expected<void, WriteError> discard() noexcept;

  /// Access the reserved slot.
  /// @note Will throw if the underlying value has been moved from.
  [[nodiscard]] std::ranges::subrange<BufferIterator> slots() const noexcept;

private:
  friend class PublisherHandle;

  /// Construct a reserved slot.
  /// @param publisher_handle A pointer to the publisher handle that reserved the slot.
  /// @param reserved_index Index to the reserved slot.
  /// @param count Number reserved.
  BatchReservedSlot(
    jewels::memory::ObjectPtr<PublisherHandle> publisher_handle, BufferIndex reserved_index, size_t count) noexcept;

  /// Handle to commit / discard the slot.
  jewels::memory::ObjectPtr<PublisherHandle> publisher_handle_;

  /// Index to the reserved slot.
  BufferIndex reserved_index_;

  /// Whether or not to commit.
  ReservationState state_;

  /// Number reserved.
  size_t count_;
};

/// Process marked slots (either for commit or discard).
/// @param slots The span of slots to process.
/// @param publish_time Time of publish to provide for all slots.
/// @return An error code if any slots fail to process.
template <size_t num_slots>
[[nodiscard]] jewels::expected<void, WriteError>
process_slots(std::span<ReservedSlot, num_slots> slots, jewels::time::SyncTime publish_time);

/// Mark a span of slots for discard.
/// @param slots The span of slots to mark.
template <size_t num_slots>
void mark_slots_for_discard(std::span<ReservedSlot, num_slots> slots);

} // namespace clockwork::pinion

#include "clockwork/pinion/publisher_handle.inl"
