// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/buffer_index.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/observer.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/slot_ref.hh"
#include "jewels/math/power_of_two.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"

#include <wise_enum.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <span>
#include <type_traits>
#include <variant>

namespace clockwork::pinion
{

namespace detail
{
///
/// An implementation for default constructed SlotRef.  Always sentinel / invalid.
///
class MonostateSlotRefWriter : public MonostateSlotRefReader
{
public:
  static inline Slot slot() noexcept;
  static inline Slot slot(std::ptrdiff_t offset) noexcept;

  static inline bool connected() noexcept;
};

///
/// An implementation of SlotRefReader to use with Buffer backed channels
///
class BufferSlotRefWriter : public BufferSlotRef<Buffer>
{
public:
  inline BufferSlotRefWriter(
    ::jewels::memory::ObjectPtr<Buffer> buffer_ptr, const BufferIterator& buffer_iterator, bool connected);

  [[nodiscard]] inline bool connected() const noexcept;

private:
  bool connected_;
};

} // namespace detail

WISE_ENUM_CLASS((ReservationState, uint8_t), discard, commit, ignore)

///
/// An abstract reference to track a writable message slot
///
class PublisherSlotRef : public detail::SlotRefBase<
                           PublisherSlotRef,
                           Slot,
                           detail::MonostateSlotRefWriter,
                           detail::BufferSlotRefWriter
                           >
{
public:
  inline PublisherSlotRef();
  inline PublisherSlotRef(
    ::jewels::memory::ObjectPtr<Buffer> buffer_ptr, const BufferIterator& buffer_iterator, bool connected);
  /// Ask the infrastructure to publish the message.
  /// @note Publish does not happen at the time of calling this.  The
  /// actual publish is taken care of by the underlying reserved slot.
  /// @note This is a no-op if !connected().
  inline void mark_for_commit() const noexcept;

  /// Ask the to unmark for publish so that this will not be published when the reserved slot is processed.
  inline void mark_for_discard() const noexcept;

  /// Ask the infrastructure to publish the message with a fake time (sim only).
  /// @note Publish does not happen at the time of calling this.  The
  /// actual publish is taken care of by the underlying reserved slot.
  /// @note This should only be used in simulation or testing.
  /// @param fake_time The fake time to use for the publish timestamp.
  inline void sim_only_mark_for_publish_with_fake_timestamp(jewels::time::SyncTime fake_time) const noexcept;

  /// Check if this publishable is connected to a channel.
  [[nodiscard]] inline bool connected() const noexcept;

  /// Get the current reservation state.
  [[nodiscard]] inline ReservationState state() const noexcept;

private:
};

namespace detail
{
using PublisherSlotRefRange = std::ranges::subrange<PublisherSlotRef>;

///
/// An implementation for default constructed PublisherReservation.  Always sentinel / invalid.
///
class MonostateReservation
{
public:
  static inline jewels::expected<void, WriteError>
    process(PublisherSlotRefRange /*commit*/, PublisherSlotRefRange /*discard*/) noexcept;
  static inline jewels::expected<void, WriteError> discard() noexcept;
  static inline PublisherSlotRefRange slots() noexcept;
};

///
/// An implementation for Buffer reservations
///
class BufferReservation
{
public:
  inline BufferReservation(
    jewels::memory::ObjectPtr<Buffer> buffer, BufferIndex index, size_t count, bool connected) noexcept;
  [[nodiscard]] inline jewels::expected<void, WriteError>
  process(PublisherSlotRefRange commit, PublisherSlotRefRange discard) noexcept;
  [[nodiscard]] inline jewels::expected<void, WriteError> discard() noexcept;
  [[nodiscard]] inline PublisherSlotRefRange slots() const noexcept;

private:
  jewels::memory::ObjectPtr<Buffer> buffer_;
  BufferIndex index_;
  size_t count_;
  bool connected_;
};

} // namespace detail

///
/// An abstract reference to track writable message slots.  Guarantees the the slots will either be committed/pubilshed
/// or discarded once this object's lifetime ends.
///
class PublisherReservation
{
public:
  // Sentinel value for sequence_number that indicates this should be discarded
  static constexpr uint64_t sequence_number_discard = std::numeric_limits<uint64_t>::max();
  // Any value not sequence_number_discard that indicates this should be published
  static constexpr uint64_t sequence_number_commit = 1;
  // Sentinel value for publish_timestamp that indicates the publish_timestamp should be filled in at publish and wasn't
  // overridden
  static constexpr int64_t unset_publish_timestamp = std::numeric_limits<int64_t>::lowest();

  /// Create a Reservation for a Buffer based channel
  PublisherReservation(
    Observer* observer, jewels::memory::ObjectPtr<Buffer> buffer, BufferIndex index, size_t count, bool connected);

  /// Reservations can be move constructed but not copied or assigned.
  PublisherReservation(const PublisherReservation&) = delete;
  inline PublisherReservation(PublisherReservation&& other) noexcept;
  PublisherReservation& operator=(const PublisherReservation&) = delete;
  PublisherReservation& operator=(PublisherReservation&&) = delete;
  ~PublisherReservation() noexcept(false);

  /// Gets the SlotRefs of the reservation
  [[nodiscard]] inline std::ranges::subrange<PublisherSlotRef> slots() const noexcept;

  /// Returns if this reservation is valid (i.e. owns an active reservation)
  [[nodiscard]] inline bool pending() const noexcept;

  /// Return the first sequence number that will be assigned to committed slots.
  [[nodiscard]] inline uint64_t sequence_number() const noexcept;

  /// Commit or discard the message if marked.
  /// @param publish_time Used if committing to specify time of commit.
  [[nodiscard]] jewels::expected<void, WriteError> process(jewels::time::SyncTime publish_time) noexcept;

  /// Mark for commit and process in one call.
  /// @param publish_time Used to specify time of commit.
  [[nodiscard]] jewels::expected<void, WriteError> commit(jewels::time::SyncTime publish_time) noexcept;

  /// Mark for commit and process in one call overriding the message sequence number.
  /// @param publish_time Used to specify time of commit.
  /// @param sequence_number Override for the message sequence number
  /// @param source_commit_time Message commit time from the original sender
  [[nodiscard]] jewels::expected<void, WriteError> commit(
    jewels::time::SyncTime publish_time, uint64_t sequence_number, jewels::time::SyncTime source_commit_time) noexcept;

  /// Mark for discard and process in one call.
  [[nodiscard]] jewels::expected<void, WriteError> discard() noexcept;

private:
  template <typename Impl>
  PublisherReservation(Observer* observer, Impl&& impl, uint64_t sequence_number);

  /// Resets the impl to be MonostateReservation without resolving the existing reservation, if any.
  inline void unset() noexcept;

  [[nodiscard]] jewels::expected<void, WriteError> process(
    bool force_commit,
    jewels::time::SyncTime publish_time,
    uint64_t sequence_number,
    jewels::time::SyncTime source_commit_time,
    jewels::time::SyncTime latest_commit_timestamp) noexcept;

  static_assert(std::is_nothrow_move_constructible_v<detail::MonostateReservation>);
  static_assert(std::is_nothrow_move_constructible_v<detail::BufferReservation>);

  Observer* observer_;
  std::variant<
    detail::MonostateReservation,
    detail::BufferReservation
    >
    impl_;
  std::ranges::subrange<PublisherSlotRef> slots_;
  uint64_t sequence_number_;
};

/// Process marked slots (either for commit or discard).
/// @param slots The span of slots to process.
/// @param publish_time Time of publish to provide for all slots.
/// @return An error code if any slots fail to process.
template <size_t num_slots, typename ReservationType = PublisherReservation>
[[nodiscard]] jewels::expected<void, WriteError>
process_slots(std::span<ReservationType, num_slots> reservations, jewels::time::SyncTime publish_time);

} // namespace clockwork::pinion

#include "clockwork/pinion/publisher_slot_ref.inl"
