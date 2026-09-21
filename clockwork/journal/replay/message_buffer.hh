// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/journal/replay/replay_metadata.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/readers/types.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace clockwork::journal::replay
{

/// Owning typed message and log metadata buffered for replay.
template <clockwork::TappyType MessageType>
struct ReplayMessage
{
  /// Logged channel name.
  std::string channel_name;
  /// Channel sequence number.
  uint32_t sequence_number{};
  /// Message publish time.
  clockwork_logging::LogTimestamp publish_time;
  /// Deserialized payload.
  MessageType message;
};

/// Buffers messages of one type that are required for replay.
template <clockwork::TappyType MessageType>
class MessageBuffer
{
private:
  struct ChannelMessageKey
  {
    std::string channel_name;
    MessageKey message_key{};

    bool operator==(const ChannelMessageKey&) const = default;
  };

  struct ChannelMessageKeyHasher
  {
    size_t operator()(const ChannelMessageKey& key) const noexcept;
  };

  struct MessageEntry
  {
    size_t remaining_reference_count{};
    std::shared_ptr<const ReplayMessage<MessageType>> message;
  };

  using MessageMap = std::unordered_map<ChannelMessageKey, MessageEntry, ChannelMessageKeyHasher>;

public:
  /// Construct a buffer for the supplied channel requirements.
  /// @param buffer Constructed message buffer on success.
  /// @return Failure if a requirement has no references or duplicates a channel and message key.
  static jewels::BinaryOutcome
  try_make(jewels::FactoryOut<MessageBuffer> buffer, const std::vector<ChannelRequirements>& requirements);

  /// Add a typed message from a log-reader callback.
  ///
  /// Callback data is copied when the message is required. Messages not referenced by a future execution are dropped.
  /// @return True if the message was newly buffered.
  bool push(const clockwork_logging::LoggedMessage& logged_message, const MessageType& message);

  /// Return whether the buffer contains a message.
  [[nodiscard]] bool contains(const std::string& channel_name, MessageKey key) const;

  /// Return shared ownership of a buffered message.
  /// @param message Buffered message on success.
  /// @return Failure if the message is not buffered.
  jewels::BinaryOutcome get(
    jewels::Out<std::shared_ptr<const ReplayMessage<MessageType>>> message,
    const std::string& channel_name,
    MessageKey key) const;

  /// Consume one execution reference to a message.
  ///
  /// The buffer releases its shared pointer after the last execution reference is consumed.
  /// @return Failure if the message is not required by any remaining execution.
  jewels::BinaryOutcome consume(const std::string& channel_name, MessageKey key);

  /// Return the number of currently buffered messages.
  [[nodiscard]] size_t size() const noexcept;

private:
  explicit MessageBuffer(MessageMap messages);

  MessageMap messages_;
  size_t buffered_message_count_{};
};

} // namespace clockwork::journal::replay

#include "clockwork/journal/replay/message_buffer.inl"
