// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "clockwork/common/process_description.hh"
#include "clockwork/logging/log_writer_config.hh"
#include "clockwork/logging/offboard/writer.hh"
#include "clockwork/pinion/shm_publisher.hh"
#include "clockwork/runners/deterministic_channel_handler.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/uuid/uuid.hh"
#include "jewels/uuid/uuid_hasher.hh"

#include <functional>
#include <memory>
#include <memory_resource>
#include <string>
#include <string_view>
#include <unordered_map>

namespace clockwork_logging
{
using ChannelMap = std::pmr::unordered_map<
  jewels::Uuid<::clockwork::common::EndpointInstanceId>,
  std::shared_ptr<::clockwork::pinion::ShmPublisher>,
  jewels::UuidHasher<::clockwork::common::EndpointInstanceId>>;

/// Class that implements the Abstract Message Writer Interface. This class is designed to write to a log file upon
/// receiving a message.
class LogMessageWriter : public clockwork::AbstractMessageWriter
{
public:
  ///
  /// Constructor
  /// @param memory_resource Memory resource to be used
  /// @param log_writer_config Log writer config
  /// @param channels Mapping of channels that are available in the system
  /// @param log_uri URI of the log file to write to.
  LogMessageWriter(
    jewels::memory::MemoryResource memory_resource,
    jewels::memory::ObjectPtr<const clockwork_logging::LogWriterConfigTap> log_writer_config,
    ChannelMap channels,
    std::string_view log_uri);

  ///
  /// Callback called on receiving a message that's configured to be written. Writes out the message to a log file.
  /// @param message_info Info regarding the message that is to be logged.
  void message_received_callback(::clockwork::MessageInfoView message_info) override;

  ///
  /// Initialize the LogMessageWriter
  /// @return Error if initialization fails.
  jewels::expected<void, jewels::MonoError> initialize() override;

  LogMessageWriter(const LogMessageWriter&) = delete;
  LogMessageWriter& operator=(const LogMessageWriter&) = delete;
  LogMessageWriter(LogMessageWriter&&) = delete;
  LogMessageWriter& operator=(LogMessageWriter&&) = delete;
  ~LogMessageWriter() override;

private:
  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;

  /// URI of the log file to be written.
  std::pmr::string log_uri_;

  /// Log Writer Config file that specifies which channels are to be written.
  jewels::memory::ObjectPtr<const clockwork_logging::LogWriterConfigTap> log_writer_config_;

  /// Log writer
  clockwork_logging::offboard::Writer writer_;

  /// Shmem channels.
  ChannelMap channels_;
};
} // namespace clockwork_logging
