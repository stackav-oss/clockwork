// IWYU pragma: private, include "clockwork/pinion/publishable.hh"
#pragma once

#include "clockwork/pinion/publishable.hh"

#include "clockwork/pinion/publisher_handle.hh"
#include "clockwork/pinion/slot.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"

namespace clockwork::pinion
{

/// A typed wrapper around ReservedSlot that can be passed to a cog.
template <class Message>
jewels::expected<Publishable<Message>, jewels::MonoError>
Publishable<Message>::try_make(jewels::memory::ObjectPtr<ReservedSlot> reserved_slot) noexcept
{
  // Check if the cast is valid so we can store the underlying
  // reserved slot and not need to re-check later.
  if (!marshal_as<Message>(reserved_slot->slot().message()))
  {
    return jewels::unexpected{jewels::MonoError{}};
  }

  return Publishable<Message>{reserved_slot};
}

template <class Message>
Publishable<Message>::Publishable(jewels::memory::ObjectPtr<ReservedSlot> reserved_slot) noexcept
  : reserved_slot_{reserved_slot}
{
  // Default construct to get the correct default value for this type.
  // The other parts of the slot are already zero'd by the buffer
  // handle on reserve.
  detail::InitMessageData<Message>::clear(reserved_slot_->slot().message());
}

template <class Message>
Message& Publishable<Message>::message() const noexcept
{
  // This is safe to call, because the safe version was checked in try_make.
  return *unsafe_marshal_as<Message>(reserved_slot_->slot().message());
}

template <class Message>
void Publishable<Message>::mark_for_publish() noexcept
{
  reserved_slot_->mark_for_commit();
}

template <class Message>
void Publishable<Message>::sim_only_mark_for_publish_with_fake_timestamp(jewels::time::SyncTime fake_time) noexcept
{
  reserved_slot_->sim_only_mark_for_commit_with_fake_timestamp(fake_time);
}

template <class Message>
bool Publishable<Message>::is_marked_for_publish() const noexcept
{
  return reserved_slot_->state() == ReservationState::State::commit;
}

template <class Message>
bool Publishable<Message>::connected() const noexcept
{
  return reserved_slot_->connected();
}

} // namespace clockwork::pinion
