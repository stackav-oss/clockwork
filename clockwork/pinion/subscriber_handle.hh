// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/slot.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"

#include <ranges>

namespace clockwork::pinion
{

/// Wrapper around the Buffer interface used for subscribers to access
/// available messages.
class SubscriberHandle
{
public:
  /// Construct a buffer from an existing memory region.
  /// @param buffer The underlying buffer handle.
  explicit SubscriberHandle(jewels::memory::ObjectPtr<const Buffer> buffer);

  /// Get the layout of the subscribed buffer.
  [[nodiscard]] const BufferLayout& layout() const noexcept;

  /// Get the available range of messages at the time of this call.
  /// It's possible that immediately after calling this, the oldest
  /// message is already written over.  It is important to take this
  /// into consideration before reading.  Subscribers are responsible
  /// for determining which messages in this range they should
  /// consume.
  /// @return A range of available messages.
  [[nodiscard]] std::ranges::subrange<BufferIterator> available() const;

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
  /// The underlying comms buffer.
  jewels::memory::ObjectPtr<const Buffer> buffer_;
};

/// Get the newly available messages given an expected next message to
/// consume.
/// @note This only errors when the next-to-consume message is in the
/// future or in the past.  Other thresholds for falling behind are
/// not checked here.
/// @note At startup, when the next-to-consume iterator is the
/// sentinel iterator, then the available range is returned directly.
/// At the start of the system, cogs should check for this case (by
/// using is_sentinel_iterator(...)) to avoid using the entire
/// available range.  Otherwise there is a high risk some messages can
/// be overwritten during execution of the cog.
/// @param available The available messages.
/// @param next_to_consume The expected next message to consume.
/// @return A range of all elements starting from the next to consume
/// or an unexpected.
[[nodiscard]] jewels::expected<std::ranges::subrange<BufferIterator>, ProgressError>
available_starting_from(const std::ranges::subrange<BufferIterator>& available, const BufferIterator& next_to_consume);

/// Callable type to convert from a slot to a message.
template <class Message>
struct MessageCast
{
  /// Cast a slot to a message type.
  /// @param slot A buffer slot.
  /// @return A reference to the typed message.
  Message& operator()(const Slot& slot) const noexcept;
};

/// Alias for the message range type.
template <class Message>
using MessageRange = std::ranges::transform_view<std::ranges::subrange<BufferIterator>, MessageCast<Message>>;

/// Transform a buffer range to a message range.
template <class Message>
jewels::expected<MessageRange<Message>, jewels::MonoError>
to_message_range(const std::ranges::subrange<BufferIterator>& buffer_range);

/// Struct that provides both the message and the underlying slot so the metadata is accessible.
template <class Message>
struct MessageSlot
{
  const Slot slot; // it should be const since it's a ref
  Message& msg;    // this is a view struct
};

/// Functor type to convert from a slot to a MessageSlot.
template <class Message>
struct MessageSlotCast
{
  MessageSlot<Message> operator()(const Slot& slot) const noexcept;
};

/// Alias for the message range type.
template <class Message>
using MessageSlotRange = std::ranges::transform_view<std::ranges::subrange<BufferIterator>, MessageSlotCast<Message>>;

/// Transform a buffer range to a message range.
template <class Message>
jewels::expected<MessageSlotRange<Message>, jewels::MonoError>
to_message_slot_range(const std::ranges::subrange<BufferIterator>& buffer_range);

} // namespace clockwork::pinion

#include "clockwork/pinion/subscriber_handle.inl"
