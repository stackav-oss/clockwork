// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_timestamp.hh"
#include "jewels/container/circular_buffer.hh"
#include "jewels/memory/aligned_storage.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"

#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <memory_resource>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_set>

namespace clockwork_logging
{
/// Class to filter duplicate messages from the merged log
class DuplicateMessageFilter
{
public:
  /// Key in the map of recently processed messages
  struct MessageCacheEntry
  {
    /// Channel string
    std::string_view channel_str{};

    /// Sequence number
    uint32_t sequence_number{};

    /// Message publish timestamp
    LogTimestamp message_time{};

    /// Comparison operator
    /// @param[in] lhs Left hand side argument
    /// @param[in] rhs Right hand side argument
    /// @return True if lhs == rhs
    [[nodiscard]] friend bool operator==(const MessageCacheEntry& lhs, const MessageCacheEntry& rhs) = default;

    /// Comparison operator
    /// @param[in] lhs Left hand side argument
    /// @param[in] rhs Right hand side argument
    /// @return True if lhs < rhs
    [[nodiscard]] friend bool operator<(const MessageCacheEntry& lhs, const MessageCacheEntry& rhs)
    {
      return std::tie(lhs.channel_str, lhs.sequence_number, lhs.message_time) <
             std::tie(rhs.channel_str, rhs.sequence_number, rhs.message_time);
    }
  };

  /// Map from recent message hash to message contents
  using MessageCacheType = std::pmr::set<MessageCacheEntry>;

  /// Entry in the recent message circular queue
  struct MessageQueueEntry
  {
    /// Message publish timestamp
    LogTimestamp message_time;

    /// Iterator to the message entry in the recent message map
    MessageCacheType::iterator message_cache_iter;
  };

  using MessageQueueType = jewels::container::CircularBuffer<jewels::memory::ObjectPolicy<MessageQueueEntry>>;

  /// Constructor
  /// @param[in] memory_resource Memory resource
  /// @param[in] message_queue_size Maximum size of the message queue used to detect duplicates
  /// @param[in] message_queue_expiration_interval Maximum time to keep a message in the message queue
  DuplicateMessageFilter(
    jewels::memory::MemoryResource memory_resource,
    size_t message_queue_size,
    std::chrono::nanoseconds message_queue_expiration_interval);

  /// Test whether a message should be filtered as a duplicate
  /// @param[in] channel_name Channel name
  /// @param[in] message_time Message time
  /// @param[in] sequence_number Message sequence number
  /// @return True iff the message is a duplicate
  [[nodiscard]] bool is_duplicate(std::string_view channel_name, LogTimestamp message_time, uint32_t sequence_number);

  /// Get the timestamp of the earliest message in the queue
  /// @return the earliest timestamp in the cache if any
  [[nodiscard]] jewels::expected<LogTimestamp, jewels::monostate> try_get_min_timestamp();

  /// Get the timestamp of the latest message in the queue
  /// @return the latest timestamp in the cache if any
  [[nodiscard]] jewels::expected<LogTimestamp, jewels::monostate> try_get_max_timestamp();

private:
  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;

  /// Persistent storage for the channel name strings
  std::pmr::list<std::pmr::string> channel_name_strings_;

  /// Set of string views backed by the persistent channel name strings
  std::pmr::unordered_set<std::string_view> channel_name_views_;

  /// Map used to filter duplicates
  MessageCacheType message_cache_;

  /// Queue of messages contained in the message map
  MessageQueueType message_queue_;

  /// Maximum time to keep a message in the message queue
  std::chrono::nanoseconds message_queue_expiration_interval_;

  /// Helper function to pop the oldest message from the queue
  void pop_oldest();
};

} // namespace clockwork_logging
