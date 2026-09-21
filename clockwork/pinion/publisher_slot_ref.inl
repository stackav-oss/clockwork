// IWYU pragma: private, include "clockwork/pinion/publisher_slot_ref.hh"
#pragma once

#include "clockwork/pinion/publisher_slot_ref.hh"
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/buffer_index.hh" // IWYU pragma: keep
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/slot_ref.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <utility>
#include <variant>

namespace clockwork::pinion
{
namespace detail
{

[[nodiscard]] Slot MonostateSlotRefWriter::slot() noexcept
{
  return get_empty_slot();
}

[[nodiscard]] Slot MonostateSlotRefWriter::slot(std::ptrdiff_t /*offset*/) noexcept
{
  return get_empty_slot();
}

inline bool MonostateSlotRefWriter::connected() noexcept
{
  return false;
}

BufferSlotRefWriter::BufferSlotRefWriter(
  ::jewels::memory::ObjectPtr<Buffer> buffer_ptr, const BufferIterator& buffer_iterator, bool connected)
  : BufferSlotRef(buffer_ptr, buffer_iterator), connected_(connected)
{
}

inline bool BufferSlotRefWriter::connected() const noexcept
{
  return connected_;
}

jewels::expected<void, WriteError>
MonostateReservation::process(PublisherSlotRefRange /*commit*/, PublisherSlotRefRange /*discard*/) noexcept
{
  return jewels::unexpected(WriteError::unexpected_reservation);
}

jewels::expected<void, WriteError> MonostateReservation::discard() noexcept
{
  return {};
}

PublisherSlotRefRange MonostateReservation::slots() noexcept
{
  return {PublisherSlotRef{}, PublisherSlotRef{}};
}

} // namespace detail

inline PublisherSlotRef::PublisherSlotRef()
  : SlotRefBase(detail::MonostateSlotRefWriter())
{
}

inline PublisherSlotRef::PublisherSlotRef(
  ::jewels::memory::ObjectPtr<Buffer> buffer_ptr, const BufferIterator& buffer_iterator, bool connected)
  : SlotRefBase(detail::BufferSlotRefWriter(buffer_ptr, buffer_iterator, connected))
{
}

inline void PublisherSlotRef::mark_for_commit() const noexcept
{
  Slot slot = this->slot();
  slot.header()->sequence_number = PublisherReservation::sequence_number_commit;
}

inline void PublisherSlotRef::mark_for_discard() const noexcept
{
  Slot slot = this->slot();
  slot.header()->sequence_number = PublisherReservation::sequence_number_discard;
}

inline void
PublisherSlotRef::sim_only_mark_for_publish_with_fake_timestamp(jewels::time::SyncTime fake_time) const noexcept
{
  Slot slot = this->slot();
  slot.header()->sequence_number = PublisherReservation::sequence_number_commit;
  slot.header()->publish_timestamp = fake_time.time_since_epoch().count();
}

[[nodiscard]] inline bool PublisherSlotRef::connected() const noexcept
{
  return apply([](const auto& iter) { return iter.connected(); });
}

[[nodiscard]] inline ReservationState PublisherSlotRef::state() const noexcept
{
  if (is_sentinel())
  {
    return ReservationState::ignore;
  }
  Slot slot = this->slot();
  if (slot.header()->sequence_number != PublisherReservation::sequence_number_discard)
  {
    return ReservationState::commit;
  }
  return ReservationState::discard;
}

inline PublisherReservation::PublisherReservation(PublisherReservation&& other) noexcept
  : observer_(other.observer_),
    impl_(std::move(other.impl_)),
    slots_(std::move(other.slots_)),
    sequence_number_(other.sequence_number_)
{
  other.unset();
}

std::ranges::subrange<PublisherSlotRef> PublisherReservation::slots() const noexcept
{
  return slots_;
}

[[nodiscard]] inline bool PublisherReservation::pending() const noexcept
{
  return (impl_.index() != 0);
}

[[nodiscard]] inline uint64_t PublisherReservation::sequence_number() const noexcept
{
  return sequence_number_;
}

inline void PublisherReservation::unset() noexcept
{
  observer_ = nullptr;
  impl_.emplace<detail::MonostateReservation>();
  slots_ = detail::MonostateReservation::slots();
}

template <size_t num_slots, typename ReservationType>
jewels::expected<void, WriteError>
process_slots(std::span<ReservationType, num_slots> reservations, jewels::time::SyncTime publish_time)
{
  for (auto& reservation : reservations)
  {
    if (const auto result = reservation.process(publish_time); !result)
    {
      return result;
    }
  }
  return {};
}

} // namespace clockwork::pinion
