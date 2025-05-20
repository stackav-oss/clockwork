// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "clockwork/common/process_description.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_writer_config.hh"
#include "clockwork/logging/readers/abstract_log_reader.hh"
#include "clockwork/pinion/shm_publisher.hh"
#include "clockwork/runners/channel_publisher.hh"
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
#include <unordered_set>

namespace clockwork_logging
{

using ShmPublisherMap = std::pmr::unordered_map<
  jewels::Uuid<::clockwork::common::EndpointInstanceId>,
  std::shared_ptr<::clockwork::pinion::ShmPublisher>,
  jewels::UuidHasher<::clockwork::common::EndpointInstanceId>>;

/// Log Message Fetcher. Implements the MessageFetcher interface to fetch messages from a log file.
class LogMessageFetcher : public ::clockwork::MessageFetcher
{
public:
  ///
  /// Constructor
  /// @param log_uri URI of the log file to fetch from
  /// @param log_publisher_config Config file that specifies what channels wll be read from the log.
  /// @param log_interval Interval of time to be read from the log
  /// @param memory_resource Memory resource to use
  /// @param converter_context The converter context to use.
  LogMessageFetcher(
    std::string_view log_uri,
    jewels::memory::ObjectPtr<const LogWriterConfigTap> log_publisher_config,
    LogInterval log_interval,
    jewels::memory::MemoryResource memory_resource);

  ///
  /// Attempt to get the next message out of the log.
  /// @return An optional that contains the message info of the next message in the log. Null optional if there are no
  /// more messages to read.
  std::optional<::clockwork::MultiMessageInfoData> try_fetch_message() override;

  ///
  /// Initialize the log message fetcher
  /// @return jewels::expected<void, jewels::MonoError>
  jewels::expected<void, jewels::MonoError> initialize() override;

private:
  /// The memory resource.
  jewels::memory::MemoryResource memory_resource_;

  /// Configuration of the channels that need to be fetched. This is a LogWriterConfigTap
  /// simply because the types of the log publisher config and log writer config are identical.
  jewels::memory::ObjectPtr<const LogWriterConfigTap> log_publisher_config_;

  /// Input log URI
  std::string log_uri_;

  /// The interval over which messages should be extracted from the log.
  LogInterval log_interval_;

  /// The log reader.
  std::unique_ptr<AbstractLogReader> reader_;

  /// Topic set for the log messsage filter
  std::pmr::unordered_set<std::pmr::string> topics_;
};

} // namespace clockwork_logging
