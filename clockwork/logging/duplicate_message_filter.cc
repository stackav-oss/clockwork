// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/duplicate_message_filter.hh"

#include "jewels/memory/memory_resource.hh"

#include <fmt/format.h>

#include <iterator>
#include <memory_resource>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace clockwork_logging
{
namespace
{

/// Create a circular buffer for the message filter
/// @param[in] context Offload log context
/// @param[in] message_queue_size Message queue size
/// @return Circular buffer instance
/// @throws runtime_error If the circular buffer creation fails
[[nodiscard]] DuplicateMessageFilter::MessageQueueType
make_message_queue(jewels::memory::MemoryResource memory_resource, size_t message_queue_size)
{
  auto try_make_result = DuplicateMessageFilter::MessageQueueType::try_make(message_queue_size, memory_resource);
  if (!try_make_result)
  {
    auto err = fmt::format("Failed to create the recent message hash queue: {}", try_make_result.error());
    throw std::runtime_error(err);
  }
  return std::move(try_make_result).value();
}

} // namespace

DuplicateMessageFilter::DuplicateMessageFilter(
  jewels::memory::MemoryResource memory_resource,
  size_t message_queue_size,
  std::chrono::nanoseconds message_queue_expiration_interval)
  : memory_resource_(std::move(memory_resource)),
    channel_name_strings_(memory_resource_),
    channel_name_views_(memory_resource_),
    message_cache_(memory_resource_),
    message_queue_(make_message_queue(memory_resource_, message_queue_size)),
    message_queue_expiration_interval_(message_queue_expiration_interval)
{
}

[[nodiscard]] bool
DuplicateMessageFilter::is_duplicate(std::string_view channel_name, LogTimestamp message_time, uint32_t sequence_number)
{
  MessageCacheEntry message_cache_entry{
    .channel_str = channel_name,
    .sequence_number = sequence_number,
    .message_time = message_time,
  };

  // Limit the time we keep messages in the recent message map
  while (!message_queue_.empty() &&
         (message_time - message_queue_.begin()->message_time > message_queue_expiration_interval_))
  {
    pop_oldest();
  }

  // Check for duplicates
  if (message_cache_.contains(message_cache_entry))
  {
    return true;
  }

  // Limit the size of message queue by count
  if (message_queue_.full())
  {
    pop_oldest();
  }

  // Use a string view from the channel name list for the message map key
  auto channel_name_iter = channel_name_views_.find(channel_name);
  if (channel_name_iter == channel_name_views_.end())
  {
    // NOLINTNEXTLINE(modernize-use-emplace) emplace_back is less verbose than emplace(back,...)
    channel_name_strings_.emplace_back(std::pmr::string{channel_name, memory_resource_});
    channel_name_iter = channel_name_views_.emplace(channel_name_strings_.back()).first;
  }
  message_cache_entry.channel_str = *channel_name_iter;

  auto iter = message_cache_.emplace(message_cache_entry).first;
  message_queue_.emplace_back(MessageQueueEntry{.message_time = message_time, .message_cache_iter = iter});

  return false;
}

void DuplicateMessageFilter::pop_oldest()
{
  const auto& oldest_entry_iter = message_queue_.begin()->message_cache_iter;
  message_cache_.erase(oldest_entry_iter);
  message_queue_.pop_front();
}

jewels::expected<LogTimestamp, jewels::monostate> DuplicateMessageFilter::try_get_min_timestamp()
{
  if (message_queue_.empty())
  {
    return jewels::unexpected{jewels::monostate{}};
  }
  return message_queue_.begin()->message_time;
}

jewels::expected<LogTimestamp, jewels::monostate> DuplicateMessageFilter::try_get_max_timestamp()
{
  if (message_queue_.empty())
  {
    return jewels::unexpected{jewels::monostate{}};
  }
  return std::prev(message_queue_.end())->message_time;
}
} // namespace clockwork_logging
