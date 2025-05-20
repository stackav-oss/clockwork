// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "clockwork/common/process_description.hh"
#include "clockwork/logging/log_writer_config.hh"
#include "clockwork/pinion/shm_publisher.hh"
#include "clockwork/runners/deterministic_runner.hh"
#include "jewels/container/compare.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"
#include "jewels/uuid/uuid_hasher.hh"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace clockwork
{
using ShmPublisherMap = std::pmr::unordered_map<
  jewels::Uuid<::clockwork::common::EndpointInstanceId>,
  std::shared_ptr<::clockwork::pinion::ShmPublisher>,
  jewels::UuidHasher<::clockwork::common::EndpointInstanceId>>;

/// Struct representing the data+metadata to be published on the channel
/// Message data is not copied
struct MessageInfoView
{
  /// Sequence Number
  uint32_t sequence_number{};

  /// Time this message should be published
  jewels::time::SyncTime time_to_publish;

  /// Pointer to the raw message data, which should be valid for the lifetime of the object
  std::span<std::byte> data;

  /// Channel Name
  std::pmr::string channel;
};

/// Struct representing the data+metadata to be published on the channel
/// Message data is copied
struct MultiMessageInfoData
{
  /// Sequence Number
  uint32_t sequence_number{};

  /// Time this message should be published
  jewels::time::SyncTime time_to_publish;

  /// Vector raw data messages.
  std::pmr::deque<std::pmr::vector<std::byte>> msgs;

  /// Channel Name
  std::pmr::string channel;

  /// Construct the Data object by copying the info view
  static MultiMessageInfoData from_view(const MessageInfoView& info, jewels::memory::MemoryResource memory_resource)
  {
    return MultiMessageInfoData{
      .sequence_number = info.sequence_number,
      .time_to_publish{info.time_to_publish},
      .msgs{{std::pmr::vector<std::byte>{info.data.begin(), info.data.end(), memory_resource}}, memory_resource},
      .channel{info.channel}};
  }
};

/// Abstract message fetcher. Used to define an interface for obtaining messages from a source that are to be published
/// to a channel when running under the deterministic runner.
class MessageFetcher
{
public:
  ///
  /// Attempt to fetch a message from the underlying source
  /// @return The next message to publish. If there are no remaining messages, return a nullopt.
  virtual std::optional<MultiMessageInfoData> try_fetch_message() = 0;

  ///
  /// Initialize the message fetcher. Perform all setup that can potentially fail.
  /// @return A MonoError on failure, otherwise void.
  virtual jewels::expected<void, jewels::MonoError> initialize() = 0;
  virtual ~MessageFetcher() = default;

  MessageFetcher() = default;
  MessageFetcher(const MessageFetcher&) = delete;
  MessageFetcher& operator=(const MessageFetcher&) = delete;
  MessageFetcher(MessageFetcher&&) = delete;
  MessageFetcher& operator=(MessageFetcher&&) = delete;
};

/// Class used to publish logged messages from a log file to ShmChannel.
class ChannelPublisher : public clockwork::AbstractChannelPublisher
{
public:
  ///
  /// Constructor.
  /// @param memory_resource Memory resource to be used.
  /// @param log_publisher_config The log publisher config. This is a LogWriterConfigTap because the format is identical
  /// to what is needed for the log publisher.
  /// @param message_fetcher The message fetcher. Used to obtain the data to publish to the relevant channels.
  /// @param channels Map of Endpoint UUIDs to channel publishers. Does not need to be restricted to only contain
  /// channels that the log publisher is publishing to.
  ChannelPublisher(
    jewels::memory::MemoryResource memory_resource,
    jewels::memory::ObjectPtr<const clockwork_logging::LogWriterConfigTap> log_publisher_config,
    const jewels::memory::NonNullSharedPtr<MessageFetcher>& message_fetcher,
    ShmPublisherMap channels,
    bool suppress_schema_mismatch_errors);

  ChannelPublisher(const ChannelPublisher&) = delete;
  ChannelPublisher& operator=(const ChannelPublisher&) = delete;
  ChannelPublisher(ChannelPublisher&&) = delete;
  ChannelPublisher& operator=(ChannelPublisher&&) = delete;

  ///
  /// Destructor.
  ///
  ~ChannelPublisher() override = default;

  ///
  /// Initialize the publisher. Returns an error on initialization failure.
  /// @return Error code on failure
  ///
  jewels::expected<void, jewels::MonoError> initialize() override;

  ///
  /// Publish the next available message. Returns an error on failure, including if there are no additional messages to
  /// publish.
  /// @return Error code on failure
  ///
  jewels::expected<void, jewels::MonoError> publish_next_message() override;

  ///
  /// Get the log time of the next log message to be published.
  /// @return an optional containing the time of the next message to be published.
  /// This will be set to std::nullopt if there are no messages remaining.
  ///
  std::optional<jewels::time::SyncTime> try_next_message_time() override;

  ///
  /// @return true if there are messages remaining to be published.
  /// @return false if no messages remain to be published.
  bool messages_remaining() override;

private:
  /// Memory Resource
  jewels::memory::MemoryResource memory_resource_;

  /// The message fetcher used to obtain the next message.
  jewels::memory::NonNullSharedPtr<MessageFetcher> message_fetcher_;

  jewels::memory::ObjectPtr<const clockwork_logging::LogWriterConfigTap> log_publisher_config_;
  /// Potentially the next message to be published. This will be a nullopt if there are no remaining log messages to
  /// publish.
  std::optional<MultiMessageInfoData> next_message_;

  /// Map of UUIDs to channel publishers.
  ShmPublisherMap channels_;
  std::pmr::unordered_map<std::pmr::string, std::shared_ptr<::clockwork::pinion::ShmPublisher>> channel_publishers_;

  /// Get the next available message for a known publisher.
  /// @return The next message info or nullopt if there is none.
  std::optional<MultiMessageInfoData> get_next_message_info();

  /// Set of channels that have been logged as mismatched. Used to throttle errors.
  std::pmr::unordered_set<std::pmr::string> mismatched_channels_logged_;

  /// Suppress schema mismatch errors
  bool suppress_schema_mismatch_errors_;
};
} // namespace clockwork
