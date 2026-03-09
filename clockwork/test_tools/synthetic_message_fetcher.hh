// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "clockwork/runners/channel_publisher.hh"
#include "clockwork/runners/deterministic_channel_handler.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/std/span.hh"
#include "jewels/time/sync_time.hh"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory_resource>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace clockwork::testing
{

/// Metadata associated with a message retrieved from a channel.
/// Pair of a message pointer and its associated metadata.
template <typename MessageType>
struct MessageWithMetadata
{
  /// Pointer to the message
  jewels::memory::pmr_unique_ptr<MessageType> message;

  /// Sequence Number
  uint32_t sequence_number{};

  /// Time this message should be published
  jewels::time::SyncTime time_to_publish;
};

/// Implements the abstract message writer interface. Simply stores all received messages and provides a means of
/// retrieving them.
class MessageWriterContainer : public AbstractMessageWriter
{
public:
  /// Constructor
  /// @param memres Memory resource
  explicit MessageWriterContainer(jewels::memory::MemoryResource memres);

  ///
  /// Callback that is called when a message configured to be written is received.
  /// @param message_info Message Info for the message received.
  void message_received_callback(MessageInfoView message_info) override;

  ///
  /// Get the next message from the container. Note that this is FIFO.
  std::optional<MultiMessageInfoData> try_pop_message();

  /// Get all of the messages on a given channel.
  /// Any messages popped with @ref try_pop_message will not be included.
  /// @tparam MessageType Tappy<Schema> Message Type associated with the desired channel.
  /// @param channel_name Name of the channel.
  /// @param start_time Minimum message publish time to include in the output.
  /// @param end_time Maximum message publish time to include in the output.
  /// @param messages List of pointers to the messages associated with the channel.
  /// @return OK if messages was successfully updated.
  template <typename MessageType>
  jewels::BinaryOutcome get_messages_from_channel(
    std::string_view channel_name,
    jewels::time::SyncTime start_time,
    jewels::time::SyncTime end_time,
    jewels::Out<std::pmr::vector<jewels::memory::pmr_unique_ptr<MessageType>>> messages) const;

  /// Get all of the messages and their metadata on a given channel.
  /// Any messages popped with @ref try_pop_message will not be included.
  /// @tparam MessageType Tappy<Schema> Message Type associated with the desired channel.
  /// @param channel_name Name of the channel.
  /// @param start_time Minimum message publish time to include in the output.
  /// @param end_time Maximum message publish time to include in the output.
  /// @param messages List of messages with metadata associated with the channel.
  /// @return OK if messages was successfully updated.
  template <typename MessageType>
  jewels::BinaryOutcome get_messages_and_metadata_from_channel(
    std::string_view channel_name,
    jewels::time::SyncTime start_time,
    jewels::time::SyncTime end_time,
    jewels::Out<std::pmr::vector<MessageWithMetadata<MessageType>>> messages) const;

  ///
  /// Initialize the message writer. Perform all setup that can potentially fail.
  /// @return Error on initialization failure.
  jewels::expected<void, jewels::MonoError> initialize() override;

private:
  /// The memory resource.
  jewels::memory::MemoryResource memory_resource_;

  /// Storage for the underlying messages
  std::pmr::deque<MultiMessageInfoData> received_messages_;
};

const auto cmp = [](const auto& lhs, const auto& rhs) { return lhs.time_to_publish < rhs.time_to_publish; };
using MessageInfoSet = std::multiset<MultiMessageInfoData, decltype(cmp)>;

/// Implements the message fetcher interface. Used to inject messages into the clockwork system.
class SyntheticMessageFetcher : public MessageFetcher
{
public:
  /// Constructor
  /// @param memres Memory resource
  explicit SyntheticMessageFetcher(jewels::memory::MemoryResource memres);
  ///
  /// Attempt to get the next message.
  /// @return An optional that contains the message info of the next message. Null optional if there are no
  /// more messages to read.
  std::optional<MultiMessageInfoData> try_fetch_message() override;

  ///
  /// Initialize the  message fetcher
  /// @return jewels::expected<void, jewels::MonoError>
  jewels::expected<void, jewels::MonoError> initialize() override;

  ///
  /// Reset the fetcher to the beginning of the message stream.
  jewels::BinaryOutcome reset() noexcept final;

  /// Add a message to the message fetcher
  /// @tparam MessageType TAP message
  /// @param message Message to be added.
  /// @param publish_time The simulated publish time for the message
  /// @param channel_name Name of the channel for the message to be published on.
  template <typename MessageType>
  void add_message(const MessageType& message, jewels::time::SyncTime publish_time, std::string_view channel_name)
  {
    const auto message_bytes = as_bytes(jewels::as_single_item_span(message));
    auto data_vec = std::pmr::vector<std::byte>(message_bytes.size(), memory_resource_);
    std::copy(message_bytes.begin(), message_bytes.end(), data_vec.begin());
    auto added_message = MultiMessageInfoData{
      .sequence_number = sequence_count_,
      .time_to_publish = publish_time,
      .msgs = {{std::move(data_vec)}, memory_resource_},
      .channel = std::pmr::string{channel_name, memory_resource_},
    };
    messages_.emplace(std::move(added_message));
    ++sequence_count_;
  }

private:
  /// Sequence number for the underlying message
  uint32_t sequence_count_ = 0;

  /// Underlying memory resource
  jewels::memory::MemoryResource memory_resource_;

  /// Stores the message data ordered by publish time.
  MessageInfoSet messages_;

  /// Iterator pointing to the current message to fetch
  MessageInfoSet::iterator current_it_;
};
} // namespace clockwork::testing

#include "clockwork/test_tools/synthetic_message_fetcher.inl"
