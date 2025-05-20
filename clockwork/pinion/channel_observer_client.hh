// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/pinion/buffer.hh"
#include "jewels/time/sync_time.hh"

#include <string_view>

namespace clockwork::pinion
{

/// Interface implemented by channel observer clients to receive notifications
class ChannelObserverClient
{
public:
  ChannelObserverClient() = default;

  /// Destructor does a clean shutdown
  virtual ~ChannelObserverClient() = default;

  ChannelObserverClient(const ChannelObserverClient&) = default;
  ChannelObserverClient& operator=(const ChannelObserverClient&) = default;
  ChannelObserverClient(ChannelObserverClient&&) = default;
  ChannelObserverClient& operator=(ChannelObserverClient&&) = default;

  /// Process a message notification from an observer
  /// @param[in] channel_name Channel name
  /// @param[in] buffer_ptr Pinion buffer pointer
  /// @param[in] buffer_iterator Pinion buffer iterator
  virtual void message_callback(
    jewels::time::SyncTime current_time,
    std::string_view channel_name,
    jewels::memory::ObjectPtr<const clockwork::pinion::Buffer> buffer_ptr,
    const clockwork::pinion::BufferIterator& buffer_iterator) = 0;

  /// Process a message drop notification from an observer
  /// @param[in] channel_name Channel name
  /// param[in] drop_count Number of messages dropped
  virtual void drop_callback(std::string_view channel_name, size_t drop_count) = 0;
};

} // namespace clockwork::pinion
