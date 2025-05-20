// IWYU pragma: private, include "clockwork/pinion/subscriber_handle.hh"
#pragma once

#include "clockwork/pinion/subscriber_handle.hh"

#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/slot.hh"
#include "jewels/std/expected.hh"

#include <boost/iterator/iterator_facade.hpp>

#include <cstddef>
#include <iterator>
#include <ranges>
#include <span>

namespace clockwork::pinion
{

template <class Message>
Message& MessageCast<Message>::operator()(const Slot& slot) const noexcept
{
  return *detail::marshal_as<Message>(std::span<const std::byte, sizeof(Message)>{slot.message()});
}

template <class Message>
jewels::expected<MessageRange<Message>, jewels::MonoError>
to_message_range(const std::ranges::subrange<BufferIterator>& buffer_range)
{
  if (!std::ranges::empty(buffer_range) && buffer_range.front().message().size() != sizeof(Message))
  {
    return jewels::unexpected{jewels::MonoError{}};
  }
  return MessageRange<Message>{buffer_range, MessageCast<Message>{}};
}

template <class Message>
MessageSlot<Message> MessageSlotCast<Message>::operator()(const Slot& slot) const noexcept
{
  return MessageSlot<Message>{
    .slot = slot, .msg = *detail::marshal_as<Message>(std::span<const std::byte, sizeof(Message)>{slot.message()})};
}

template <class Message>
jewels::expected<MessageSlotRange<Message>, jewels::MonoError>
to_message_slot_range(const std::ranges::subrange<BufferIterator>& buffer_range)
{
  if (!std::ranges::empty(buffer_range) && buffer_range.front().message().size() != sizeof(Message))
  {
    return jewels::unexpected{jewels::MonoError{}};
  }
  return MessageSlotRange<Message>{buffer_range, MessageSlotCast<Message>{}};
}

} // namespace clockwork::pinion
