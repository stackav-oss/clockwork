// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/logging/channel_publisher_config_clk_cc.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_playback/tachyon_upgrader.hh"
#include "clockwork/logging/readers/abstract_log_reader.hh"
#include "clockwork/pinion/shm_publisher.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/runners/channel_publisher.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"
#include "jewels/uuid/uuid_hasher.hh"

#include <functional>
#include <memory>
#include <memory_resource>
#include <optional>
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

// End of log channel name. Must match the channel name defined in end_of_log_channel.clk
constexpr auto end_of_log_channel_name = "/clockwork_logging/end_of_log";

/// Log Message Fetcher. Implements the MessageFetcher interface to fetch messages from a log file.
class LogMessageFetcher : public ::clockwork::MessageFetcher
{
public:
  ///
  /// Constructor
  /// @param log_uri URI of the log file to fetch from
  /// @param channel_publisher_config Config file that specifies what channels wll be read from the log.
  /// @param maybe_log_interval Interval of time to be read from the log
  /// @param memory_resource Memory resource to use
  /// @param converter_context The converter context to use.
  LogMessageFetcher(
    std::string_view log_uri,
    jewels::memory::ObjectPtr<const clockwork::Tappy<ChannelPublisherConfig<>>> channel_publisher_config,
    std::optional<LogInterval> maybe_log_interval,
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

  ///
  /// Reset the fetcher to the beginning of the message stream.
  jewels::BinaryOutcome reset() noexcept final;

private:
  /// Attempt to get the end_of_log message, if it has not already been sent.
  [[nodiscard]] std::optional<clockwork::MultiMessageInfoData> get_end_of_log_message();
  /// The memory resource.
  jewels::memory::MemoryResource memory_resource_;

  /// Configuration of the channels that need to be fetched.
  jewels::memory::ObjectPtr<const clockwork::Tappy<ChannelPublisherConfig<>>> channel_publisher_config_;

  /// Input log URI
  std::string log_uri_;

  /// The interval over which messages should be extracted from the log.
  std::optional<LogInterval> maybe_log_interval_;

  /// The log reader.
  std::unique_ptr<AbstractLogReader> reader_;

  /// The tachyon upgreader.
  std::unique_ptr<TachyonUpgrader> upgrader_;

  /// Topic set for the log message filter
  std::pmr::unordered_set<std::pmr::string> topics_;

  /// Time of the last message that was read from the log.
  std::optional<jewels::time::SyncTime> last_message_time_;

  /// Whether the end of log message has been sent
  bool end_of_log_sent_ = false;
};

} // namespace clockwork_logging
