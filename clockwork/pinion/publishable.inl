// IWYU pragma: private, include "clockwork/pinion/publishable.hh"
#pragma once

#include "clockwork/pinion/publishable.hh"

#include "clockwork/pinion/device_ptr.hh"
#include "clockwork/pinion/publisher_slot_ref.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/slot_ref.hh" // IWYU pragma: keep
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <new>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>

namespace clockwork::pinion
{
namespace detail
{

template <class Message>
concept HasClearMethod = requires(Message& message) {
  { message.clear() };
};

template <typename Message>
struct InitMessageData
{
  static void clear(std::span<std::byte> msg)
  {
    if constexpr (HasClearMethod<Message>)
    {
      unsafe_marshal_as<Message>(msg)->clear();
    }
    else
    {
      new (msg.data()) Message{};
    }
  }
};

} // namespace detail

/// A typed wrapper around ReservedSlot that can be passed to a cog.
template <class Message, size_t capacity>
jewels::expected<Publishable<Message, capacity>, jewels::MonoError> Publishable<Message, capacity>::try_make_from_slots(
  std::ranges::subrange<PublisherSlotRef> reserved_slot_refs, std::optional<uint64_t> first_sequence_number) noexcept
{
  if (reserved_slot_refs.size() != capacity)
  {
    return jewels::unexpected{jewels::MonoError{}};
  }

  // Check if the casts are valid so we can store the underlying
  // reserved slots and not need to re-check later.
  if (!std::ranges::all_of(
        reserved_slot_refs | to_message, [](auto bytes) { return static_cast<bool>(marshal_as<Message>(bytes)); }))
  {
    return jewels::unexpected{jewels::MonoError{}};
  }

  return Publishable<Message, capacity>{reserved_slot_refs, first_sequence_number};
}

template <class Message, size_t capacity>
jewels::expected<Publishable<Message, capacity>, jewels::MonoError>
Publishable<Message, capacity>::try_make(std::ranges::subrange<PublisherSlotRef> reserved_slot_refs) noexcept
{
  return try_make_from_slots(reserved_slot_refs, std::nullopt);
}

template <class Message, size_t capacity>
jewels::expected<Publishable<Message, capacity>, jewels::MonoError>
Publishable<Message, capacity>::try_make(PublisherSlotRef reserved_slot_ref) noexcept
  requires(capacity == 1)
{
  return try_make_from_slots({reserved_slot_ref, reserved_slot_ref + 1}, std::nullopt);
}

template <class Message, size_t capacity>
jewels::expected<Publishable<Message, capacity>, jewels::MonoError>
Publishable<Message, capacity>::try_make(jewels::memory::ObjectPtr<PublisherReservation> reservation) noexcept
{
  return try_make_from_slots(reservation->slots(), std::optional<uint64_t>{reservation->sequence_number()});
}

template <class Message, size_t capacity>
Publishable<Message, capacity>::Publishable(
  std::ranges::subrange<PublisherSlotRef> reserved_slots, std::optional<uint64_t> first_sequence_number) noexcept
  : reserved_slots_{reserved_slots.begin(), reserved_slots.end()}, metrics_first_sequence_number_{first_sequence_number}
{
  // Default construct to get the correct default value for this type.
  // The other parts of the slot are already zero'd by the buffer
  // handle on reserve.
  std::ranges::for_each(reserved_slots_ | to_message, detail::InitMessageData<Message>::clear);
}

template <class Message, size_t capacity>
constexpr size_t Publishable<Message, capacity>::size() noexcept
{
  return capacity;
}

template <class Message, size_t capacity>
std::array<Message*, capacity> Publishable<Message, capacity>::messages() const noexcept
{
  std::array<Message*, capacity> result;
  // unsafe_marshal_as is safe to call, because the safe version was checked in try_make.
  std::ranges::copy(reserved_slots_ | to_message | std::views::transform(unsafe_marshal_as<Message>), result.begin());
  return result;
}

template <class Message, size_t capacity>
Message& Publishable<Message, capacity>::message(size_t index) const
{
  if (index >= capacity) [[unlikely]]
  {
    throw std::out_of_range{"Publishable message index out of range"};
  }
  // This is safe to call, because the safe version was checked in try_make.
  return *unsafe_marshal_as<Message>(reserved_slots_[static_cast<ptrdiff_t>(index)]->message());
}

template <class Message, size_t capacity>
Message& Publishable<Message, capacity>::message() const noexcept
  requires(capacity == 1)
{
  return message(0);
}

template <class Message, size_t capacity>
[[nodiscard]] DevicePtr<Message> Publishable<Message, capacity>::device_ptr(size_t index) const
{
  if (index >= capacity) [[unlikely]]
  {
    throw std::out_of_range{"Publishable device pointer index out of range"};
  }
  // This is safe to call, because the safe version was checked in try_make and if it's okay on CPU then it's okay for
  // accelerators.
  return reinterpret_pointer_cast<Message>(reserved_slots_[static_cast<ptrdiff_t>(index)]->device_ptr());
}

template <class Message, size_t capacity>
[[nodiscard]] DevicePtr<Message> Publishable<Message, capacity>::device_ptr() const noexcept
  requires(capacity == 1)
{
  return device_ptr(0);
}

template <class Message, size_t capacity>
size_t Publishable<Message, capacity>::get_effective_count(size_t publish_count) const
{
  publish_count = std::min(publish_count, capacity);
  const auto effective_count = std::max(marked_count_.value_or(publish_count), publish_count);
  if (effective_count != publish_count)
  {
    jewels::log_cerr_error(
      "mark_for_publish called multiple times with different values ({} and {}); using the largest value.",
      effective_count,
      publish_count);
  }
  return effective_count;
}

template <class Message, size_t capacity>
void Publishable<Message, capacity>::mark_for_publish(size_t publish_count) noexcept
{
  const auto effective_count = get_effective_count(publish_count);
  marked_count_ = effective_count;
  for (const auto& slot : reserved_slots_ | std::views::take(effective_count))
  {
    slot.mark_for_commit();
  }
}

template <class Message, size_t capacity>
void Publishable<Message, capacity>::mark_all_for_publish() noexcept
{
  mark_for_publish(capacity);
}

template <class Message, size_t capacity>
void Publishable<Message, capacity>::mark_for_publish() noexcept
  requires(capacity == 1)
{
  mark_all_for_publish();
}

template <class Message, size_t capacity>
void Publishable<Message, capacity>::sim_only_mark_for_publish_with_fake_timestamp(
  size_t publish_count, jewels::time::SyncTime fake_time) noexcept
{
  const auto effective_count = get_effective_count(publish_count);
  marked_count_ = effective_count;
  for (const auto& slot : reserved_slots_ | std::views::take(effective_count))
  {
    slot.sim_only_mark_for_publish_with_fake_timestamp(fake_time);
  }
}

template <class Message, size_t capacity>
void Publishable<Message, capacity>::sim_only_mark_for_publish_with_fake_timestamp(
  jewels::time::SyncTime fake_time) noexcept
  requires(capacity == 1)
{
  sim_only_mark_for_publish_with_fake_timestamp(1, fake_time);
}

template <class Message, size_t capacity>
bool Publishable<Message, capacity>::is_marked_for_publish() const noexcept
{
  return reserved_slots_.front().state() == ReservationState::commit;
}

template <class Message, size_t capacity>
size_t Publishable<Message, capacity>::get_metrics_publish_count() const noexcept
{
  return marked_count_.value_or(0U);
}

template <class Message, size_t capacity>
std::optional<uint64_t> Publishable<Message, capacity>::get_metrics_first_sequence_number() const noexcept
{
  if (get_metrics_publish_count() == 0U)
  {
    return std::nullopt;
  }
  return metrics_first_sequence_number_;
}

template <class Message, size_t capacity>
bool Publishable<Message, capacity>::connected() const noexcept
{
  return std::ranges::any_of(reserved_slots_, &PublisherSlotRef::connected);
}

} // namespace clockwork::pinion
