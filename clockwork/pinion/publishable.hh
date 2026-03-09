// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/buffer_index.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/observer.hh"
#include "clockwork/pinion/publisher_handle.hh"
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

/// A typed wrapper around ReservedSlot that can be passed to a cog.
template <class Message>
class Publishable
{
public:
  /// Try to construct a publishable message.
  /// @note Will only return a valid message if marshal_as(...) would
  /// return a valid pointer.
  /// @param reserved_slot The reserved slot to construct from.
  [[nodiscard]] static jewels::expected<Publishable<Message>, jewels::MonoError>
  try_make(jewels::memory::ObjectPtr<ReservedSlot> reserved_slot) noexcept;

  /// Get the underlying message.
  [[nodiscard]] Message& message() const noexcept;

  /// Ask the infrastructure to publish the message.
  /// @note Publish does not happen at the time of calling this.  The
  /// actual publish is taken care of by the underlying reserved slot.
  /// @note This is a no-op if !connected().
  void mark_for_publish() noexcept;

  /// Ask the infrastructure to publish the message with a fake time (sim only).
  /// @note Publish does not happen at the time of calling this.  The
  /// actual publish is taken care of by the underlying reserved slot.
  /// @note This should only be used in simulation or testing.
  /// @param fake_time The fake time to use for the publish timestamp.
  void sim_only_mark_for_publish_with_fake_timestamp(jewels::time::SyncTime fake_time) noexcept;

  /// Check if this publishable is marked for publish.
  [[nodiscard]] bool is_marked_for_publish() const noexcept;

  /// Check if this publishable is connected to a channel.
  [[nodiscard]] bool connected() const noexcept;

private:
  /// Construct a publishable message.
  /// @param reserved_slot The reserved slot.
  explicit Publishable(jewels::memory::ObjectPtr<ReservedSlot> reserved_slot) noexcept;

  /// The underlying reserved slot.
  jewels::memory::ObjectPtr<ReservedSlot> reserved_slot_;
};

} // namespace clockwork::pinion

#include "clockwork/pinion/publishable.inl"
