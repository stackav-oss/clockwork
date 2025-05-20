// IWYU pragma: private, include "clockwork/pinion/publisher_handle.hh"
#pragma once

#include "clockwork/pinion/publisher_handle.hh"

#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/slot.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"

#include <cstring>
#include <new>
#include <span>

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
  new (reserved_slot_->slot().message().data()) Message{};
}

template <class Message>
Message& Publishable<Message>::message() const noexcept
{
  // This is safe to call, because the safe version was checked in try_make.
  return *unsafe_marshal_as<Message>(reserved_slot_->slot().message());
}

template <class Message>
void Publishable<Message>::mark_for_publish() const noexcept
{
  reserved_slot_->mark_for_commit();
}

template <size_t num_slots>
jewels::expected<void, WriteError>
process_slots(std::span<ReservedSlot, num_slots> slots, jewels::time::SyncTime publish_time)
{
  for (auto& slot : slots)
  {
    if (const auto result = slot.process(publish_time); !result)
    {
      return result;
    }
  }
  return {};
}

template <size_t num_slots>
void mark_slots_for_discard(std::span<ReservedSlot, num_slots> slots)
{
  for (auto& slot : slots)
  {
    slot.mark_for_discard();
  }
}

} // namespace clockwork::pinion
