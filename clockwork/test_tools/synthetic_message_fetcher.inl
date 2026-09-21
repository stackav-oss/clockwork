// IWYU pragma: private, include "clockwork/test_tools/synthetic_message_fetcher.hh"
#pragma once

#include "clockwork/test_tools/synthetic_message_fetcher.hh"

#include "clockwork/runners/channel_publisher.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/time/sync_time.hh"

#include <chrono>
#include <compare>
#include <cstddef>
#include <cstring>
#include <memory>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace clockwork::testing
{

template <typename MessageType>
jewels::BinaryOutcome MessageWriterContainer::get_messages_and_metadata_from_channel(
  std::string_view channel_name,
  jewels::time::SyncTime start_time,
  jewels::time::SyncTime end_time,
  jewels::Out<std::pmr::vector<MessageWithMetadata<MessageType>>> messages) const
{
  for (const auto& message_info : received_messages_)
  {
    if (
      message_info.channel != channel_name || message_info.time_to_publish < start_time ||
      message_info.time_to_publish > end_time)
    {
      continue;
    }
    if (message_info.msgs.empty())
    {
      jewels::log_cerr_error("MultiMessageInfoData message queue was empty trying to extract a message");
      return jewels::failure;
    }

    const auto& data = message_info.msgs.front();

    auto message_span = std::span<const std::byte>(data);
    if (message_span.size_bytes() != sizeof(MessageType))
    {
      jewels::log_cerr_error(
        "Size mismatch attempting to extract a message (Expected {}, got {})",
        sizeof(MessageType),
        message_span.size_bytes());
      return jewels::failure;
    }

    auto message_pointer = jewels::memory::make_pmr_unique<MessageType>(memory_resource_);
    std::memcpy(message_pointer.get(), message_span.data(), sizeof(MessageType));
    messages->push_back(
      MessageWithMetadata<MessageType>{
        .message = std::move(message_pointer),
        .sequence_number = message_info.sequence_number,
        .time_to_publish = message_info.time_to_publish,
      });
  }
  return jewels::success;
}

template <typename MessageType>
jewels::BinaryOutcome MessageWriterContainer::get_messages_from_channel(
  std::string_view channel_name,
  jewels::time::SyncTime start_time,
  jewels::time::SyncTime end_time,
  jewels::Out<std::pmr::vector<jewels::memory::pmr_unique_ptr<MessageType>>> messages) const
{
  std::pmr::vector<MessageWithMetadata<MessageType>> messages_with_metadata{messages->get_allocator()};
  if (jewels::fails(get_messages_and_metadata_from_channel(
        channel_name, start_time, end_time, jewels::Out{messages_with_metadata})))
  {
    return jewels::failure;
  }
  messages->reserve(messages_with_metadata.size());
  for (auto& entry : messages_with_metadata)
  {
    messages->push_back(std::move(entry.message));
  }
  return jewels::success;
}
} // namespace clockwork::testing
