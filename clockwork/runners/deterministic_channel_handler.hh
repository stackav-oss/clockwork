// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/process_description.hh"
#include "clockwork/logging/log_writer_config.hh"
#include "clockwork/pinion/channel_observer.hh"
#include "clockwork/pinion/channel_observer_client.hh"
#include "clockwork/pinion/shm_publisher.hh"
#include "clockwork/runners/channel_publisher.hh"
#include "jewels/container/compare.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"
#include "jewels/uuid/uuid_hasher.hh"

#include <cstddef>
#include <functional>
#include <memory>
#include <memory_resource>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace clockwork
{

/// Abstraction for underlying message writer. Derived classes are used to handle the messages subscribed to in the log
/// writer config. Some examples may include writing to a log file, or simply recording in a vector.
class AbstractMessageWriter
{
public:
  ///
  /// Callback that is called when a message configured to be written is received.
  /// @param message_info Message Info View for the message received.
  virtual void message_received_callback(MessageInfoView message_info) = 0;

  ///
  /// Initialize the message writer. Perform all setup that can potentially fail.
  /// @return Error on initialization failure.
  virtual jewels::expected<void, jewels::MonoError> initialize() = 0;
  virtual ~AbstractMessageWriter() = default;

  AbstractMessageWriter() = default;
  AbstractMessageWriter(const AbstractMessageWriter&) = delete;
  AbstractMessageWriter& operator=(const AbstractMessageWriter&) = delete;
  AbstractMessageWriter(AbstractMessageWriter&&) = delete;
  AbstractMessageWriter& operator=(AbstractMessageWriter&&) = delete;
};

using ChannelMap = std::pmr::unordered_map<
  jewels::Uuid<::clockwork::common::EndpointInstanceId>,
  std::shared_ptr<::clockwork::pinion::ShmPublisher>,
  jewels::UuidHasher<::clockwork::common::EndpointInstanceId>>;

// Class used in single process simulations and tests to log all messages published to configured channels. Simply
// instantiating this and calling initialize kicks off the log writing.
class DeterministicChannelHandler final : public pinion::ChannelObserverClient
{
public:
  ///
  /// Constructor
  /// @param memory_resource Memory resource to be used.
  /// @param message_writer Underlying message writer that handles what to do with incoming messages
  /// @param log_writer_config Configuration that dictates which channels will be written
  /// @param channels Channels used in the system
  DeterministicChannelHandler(
    jewels::memory::MemoryResource memory_resource,
    jewels::memory::NonNullSharedPtr<AbstractMessageWriter> message_writer,
    jewels::memory::ObjectPtr<const clockwork_logging::LogWriterConfigTap> log_writer_config,
    ChannelMap channels);

  DeterministicChannelHandler(const DeterministicChannelHandler&) = delete;
  DeterministicChannelHandler& operator=(const DeterministicChannelHandler&) = delete;
  DeterministicChannelHandler(DeterministicChannelHandler&&) = delete;
  DeterministicChannelHandler& operator=(DeterministicChannelHandler&&) = delete;

  ///
  /// Initialize the log writer and bind to the relevant channels.
  /// @return Error on initialization failure
  jewels::expected<void, jewels::MonoError> initialize();

  // Called by the channel observer to log new messages published to the channel.

  ///
  /// Callback called on receiving a message.
  /// @param current_time Current time
  /// @param channel_name Channel name
  /// @param buffer_ptr Pointer to the underlying pinion buffer (unused in this override)
  /// @param buffer_iterator Pinion buffer iterator
  void message_callback(
    jewels::time::SyncTime current_time,
    std::string_view channel_name,
    jewels::memory::ObjectPtr<const clockwork::pinion::Buffer> buffer_ptr,
    const clockwork::pinion::BufferIterator& buffer_iterator) override;

  ///
  /// Called by the channel observer when it has been detected that the buffer has dropped messages.
  /// @param channel_name Channel Name
  /// @param drop_count Number of messages dropped
  void drop_callback(std::string_view channel_name, size_t drop_count) final;

  ~DeterministicChannelHandler() override = default;

private:
  /// Memory resource used.
  jewels::memory::MemoryResource memory_resource_;

  /// Underlying message writer.
  jewels::memory::NonNullSharedPtr<AbstractMessageWriter> message_writer_;

  /// Log writer config.
  jewels::memory::ObjectPtr<const clockwork_logging::LogWriterConfigTap> log_writer_config_;

  /// Observers that call our message callback
  std::pmr::vector<clockwork::pinion::ChannelObserver> observers_;

  /// Shm Channels
  ChannelMap channels_;
};
} // namespace clockwork
