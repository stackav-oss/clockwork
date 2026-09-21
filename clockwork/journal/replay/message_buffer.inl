// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

// IWYU pragma: private, include "clockwork/journal/replay/message_buffer.hh"

#pragma once

#include "clockwork/journal/replay/message_buffer.hh"

#include "clockwork/journal/replay/replay_metadata.hh"
#include "clockwork/logging/readers/types.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"

#include <boost/container_hash/hash.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace clockwork::journal::replay
{

template <clockwork::TappyType MessageType>
size_t MessageBuffer<MessageType>::ChannelMessageKeyHasher::operator()(const ChannelMessageKey& key) const noexcept
{
  auto seed = size_t{};
  boost::hash_combine(seed, std::hash<std::string_view>{}(key.channel_name));
  boost::hash_combine(seed, key.message_key);
  return seed;
}

template <clockwork::TappyType MessageType>
MessageBuffer<MessageType>::MessageBuffer(MessageMap messages)
  : messages_(std::move(messages))
{
}

template <clockwork::TappyType MessageType>
jewels::BinaryOutcome MessageBuffer<MessageType>::try_make(
  jewels::FactoryOut<MessageBuffer> buffer, const std::vector<ChannelRequirements>& requirements)
{
  auto messages = MessageMap{};
  for (const auto& channel : requirements)
  {
    for (const auto& message : channel.messages)
    {
      if (message.reference_count == 0U)
      {
        return jewels::failure;
      }
      const auto inserted = messages
                              .emplace(
                                ChannelMessageKey{
                                  .channel_name = channel.channel_name,
                                  .message_key = message.key,
                                },
                                MessageEntry{
                                  .remaining_reference_count = message.reference_count,
                                  .message = {},
                                })
                              .second;
      if (!inserted)
      {
        return jewels::failure;
      }
    }
  }
  *buffer = MessageBuffer{std::move(messages)};
  return jewels::success;
}

template <clockwork::TappyType MessageType>
bool MessageBuffer<MessageType>::push(
  const clockwork_logging::LoggedMessage& logged_message, const MessageType& message)
{
  const auto key = ChannelMessageKey{
    .channel_name = std::string{logged_message.topic},
    .message_key = logged_message.sequence_number,
  };
  const auto message_iter = messages_.find(key);
  if (message_iter == messages_.end() || message_iter->second.message)
  {
    return false;
  }
  message_iter->second.message = std::make_shared<ReplayMessage<MessageType>>(ReplayMessage<MessageType>{
    .channel_name = std::string{logged_message.topic},
    .sequence_number = logged_message.sequence_number,
    .publish_time = logged_message.publish_time,
    .message = message,
  });
  ++buffered_message_count_;
  return true;
}

template <clockwork::TappyType MessageType>
bool MessageBuffer<MessageType>::contains(const std::string& channel_name, const MessageKey key) const
{
  const auto message_iter = messages_.find(
    ChannelMessageKey{
      .channel_name = channel_name,
      .message_key = key,
    });
  return message_iter != messages_.end() && message_iter->second.message;
}

template <clockwork::TappyType MessageType>
jewels::BinaryOutcome MessageBuffer<MessageType>::get(
  jewels::Out<std::shared_ptr<const ReplayMessage<MessageType>>> message,
  const std::string& channel_name,
  const MessageKey key) const
{
  const auto message_iter = messages_.find(
    ChannelMessageKey{
      .channel_name = channel_name,
      .message_key = key,
    });
  if (message_iter == messages_.end() || !message_iter->second.message)
  {
    return jewels::failure;
  }
  *message = message_iter->second.message;
  return jewels::success;
}

template <clockwork::TappyType MessageType>
jewels::BinaryOutcome MessageBuffer<MessageType>::consume(const std::string& channel_name, const MessageKey key)
{
  const auto full_key = ChannelMessageKey{
    .channel_name = channel_name,
    .message_key = key,
  };
  const auto message_iter = messages_.find(full_key);
  if (message_iter == messages_.end() || message_iter->second.remaining_reference_count == 0U)
  {
    return jewels::failure;
  }
  --message_iter->second.remaining_reference_count;
  if (message_iter->second.remaining_reference_count == 0U)
  {
    if (message_iter->second.message)
    {
      --buffered_message_count_;
    }
    messages_.erase(message_iter);
  }
  return jewels::success;
}

template <clockwork::TappyType MessageType>
size_t MessageBuffer<MessageType>::size() const noexcept
{
  return buffered_message_count_;
}

} // namespace clockwork::journal::replay
