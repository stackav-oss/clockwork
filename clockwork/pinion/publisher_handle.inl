// IWYU pragma: private, include "clockwork/pinion/publisher_handle.hh"
#pragma once

#include "clockwork/pinion/publisher_handle.hh"

#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/slot.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"

#include <cstddef>
#include <new>
#include <span>
#include <type_traits>

namespace clockwork::pinion
{
namespace detail
{
template <typename Message, typename = void>
struct InitMessageData
{
  static void clear(std::span<std::byte> msg)
  {
    new (msg.data()) Message{};
  }
};

template <typename Message>
struct InitMessageData<Message, std::void_t<decltype(Message::clear())>>
{
  static void clear(std::span<std::byte> msg)
  {
    unsafe_marshal_as<Message>(msg)->clear();
  }
};

} // namespace detail

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
