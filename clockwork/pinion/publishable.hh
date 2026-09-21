// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/pinion/abstract_channel.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/buffer_index.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/observer.hh"
#include "clockwork/pinion/publisher_slot_ref.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>

namespace clockwork::pinion
{

/// A typed wrapper around ReservedSlot that can be passed to a cog.
template <class Message, size_t capacity = 1>
class Publishable
{
public:
  /// Try to construct a publishable batch of messages.
  /// @note Will only return valid messages if marshal_as(...) would
  /// return a valid pointer for each.
  /// @param reserved_slot_refs The reserved slots to construct from.
  [[nodiscard]] static jewels::expected<Publishable, jewels::MonoError>
  try_make(std::ranges::subrange<PublisherSlotRef> reserved_slot_refs) noexcept;

  /// Try to construct a publishable message.
  /// @note Will only return a valid message if marshal_as(...) would
  /// return a valid pointer.
  /// @param reserved_slot The reserved slot to construct from.
  [[nodiscard]] static jewels::expected<Publishable, jewels::MonoError>
  try_make(PublisherSlotRef reserved_slot_ref) noexcept
    requires(capacity == 1);

  /// Try to construct a publishable from a reservation.
  /// @note Validates that the reservation contains exactly `capacity` slots.
  /// @note Will only return valid messages if marshal_as(...) would
  /// return a valid pointer for each.
  /// @param reservation The reservation to construct from.
  [[nodiscard]] static jewels::expected<Publishable, jewels::MonoError>
  try_make(jewels::memory::ObjectPtr<PublisherReservation> reservation) noexcept;

  /// Number of typed writable messages in this batch.
  [[nodiscard]] static constexpr size_t size() noexcept;

  /// Get the underlying messages.
  [[nodiscard]] std::array<Message*, capacity> messages() const noexcept;

  /// Get the underlying message.
  /// @throws std::out_of_range if index is not less than capacity.
  [[nodiscard]] Message& message(size_t index) const;

  /// Get the underlying message.
  [[nodiscard]] Message& message() const noexcept
    requires(capacity == 1);

  /// Ask the infrastructure to publish the first publish_count messages.
  /// @note Publish does not happen at the time of calling this.  The
  /// actual publish is taken care of by the underlying reserved slot.
  /// @note This is a no-op if !connected().
  /// @note If publish_count > capacity, behaviour is equivalent to mark_all_for_publish().
  /// @note If called multiple times with different values, the largest value is used and a
  /// non-fatal error is logged.
  void mark_for_publish(size_t publish_count) noexcept;

  /// Ask the infrastructure to publish all messages.
  /// @note Publish does not happen at the time of calling this.  The
  /// actual publish is taken care of by the underlying reserved slot.
  /// @note This is a no-op if !connected().
  void mark_all_for_publish() noexcept;

  /// Get a pointer to memory on an accelerator device
  /// @throws std::out_of_range if index is not less than capacity.
  [[nodiscard]] DevicePtr<Message> device_ptr(size_t index) const;

  /// Get a pointer to memory on an accelerator device
  [[nodiscard]] DevicePtr<Message> device_ptr() const noexcept
    requires(capacity == 1);

  /// Ask the infrastructure to publish the message.
  /// @note Publish does not happen at the time of calling this.  The
  /// actual publish is taken care of by the underlying reserved slot.
  /// @note This is a no-op if !connected().
  void mark_for_publish() noexcept
    requires(capacity == 1);

  /// Ask the infrastructure to publish the first publish_count messages with a fake time (sim only).
  /// @note Publish does not happen at the time of calling this.  The
  /// actual publish is taken care of by the underlying reserved slot.
  /// @note This should only be used in simulation or testing.
  /// @param fake_time The fake time to use for the publish timestamp.
  void sim_only_mark_for_publish_with_fake_timestamp(size_t publish_count, jewels::time::SyncTime fake_time) noexcept;

  /// Ask the infrastructure to publish the message with a fake time (sim only).
  /// @note Publish does not happen at the time of calling this.  The
  /// actual publish is taken care of by the underlying reserved slot.
  /// @note This should only be used in simulation or testing.
  /// @param fake_time The fake time to use for the publish timestamp.
  void sim_only_mark_for_publish_with_fake_timestamp(jewels::time::SyncTime fake_time) noexcept
    requires(capacity == 1);

  /// Check if any slot in this publishable is marked for publish.
  [[nodiscard]] bool is_marked_for_publish() const noexcept;

  /// Get the number of messages marked for publish.
  [[nodiscard]] size_t get_metrics_publish_count() const noexcept;

  /// Get the first sequence number for messages marked for publish.
  /// @note Publishables constructed without a PublisherReservation return nullopt.
  [[nodiscard]] std::optional<uint64_t> get_metrics_first_sequence_number() const noexcept;

  /// Check if this publishable is connected to any channel.
  [[nodiscard]] bool connected() const noexcept;

private:
  /// Try to construct a publishable batch of messages with optional sequence-number metadata.
  [[nodiscard]] static jewels::expected<Publishable, jewels::MonoError> try_make_from_slots(
    std::ranges::subrange<PublisherSlotRef> reserved_slot_refs, std::optional<uint64_t> first_sequence_number) noexcept;

  /// Construct a publishable message.
  /// @param reserved_slots The reserved slots.
  explicit Publishable(
    std::ranges::subrange<PublisherSlotRef> reserved_slots, std::optional<uint64_t> first_sequence_number) noexcept;

  /// The underlying reserved slots.
  std::ranges::iota_view<PublisherSlotRef, PublisherSlotRef> reserved_slots_;

  /// Tracks the publish count from the most recent mark_for_publish() call.
  std::optional<size_t> marked_count_;

  /// First sequence number assigned to the reserved slots if they are published.
  std::optional<uint64_t> metrics_first_sequence_number_;

  static constexpr auto to_message = std::views::transform(&Slot::message);

  [[nodiscard]] size_t get_effective_count(size_t publish_count) const;
};

} // namespace clockwork::pinion

#include "clockwork/pinion/publishable.inl"
