// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/decompress_option.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/offboard/reader.hh"
#include "clockwork/logging/readers/abstract_log_reader.hh"
#include "clockwork/logging/readers/types.hh"
#include "jewels/memory/memory_resource.hh"

#include <optional>
#include <string>
#include <string_view>

namespace clockwork_logging
{

/// Offboard reader implementation of the log reader interface
class OffboardLogReader : public AbstractLogReader
{
public:
  /// Constructor
  /// @param[in] log_uri Log URI
  /// @param[in] maybe_log_interval The interval to read from the log
  /// @param[in] maybe_relative_interval The interval to read from the log relative to the sart of the log
  /// @param[in] decompress_option Option for whether to decompress lite-compressed messages found in the log
  OffboardLogReader(
    std::string_view log_uri,
    std::optional<LogInterval> maybe_log_interval,
    std::optional<RelativeInterval> maybe_relative_interval,
    DecompressOption decompress_option);

  ~OffboardLogReader() override;
  OffboardLogReader(const OffboardLogReader&) = delete;
  OffboardLogReader& operator=(const OffboardLogReader&) = delete;
  OffboardLogReader(OffboardLogReader&&) = delete;
  OffboardLogReader& operator=(OffboardLogReader&&) = delete;

  /// Get the type of log reader
  /// @returns The log reader type
  [[nodiscard]] std::string type() const override;

  /// Open the log.
  /// @param[in] topic_filter Optional topic filter, return false if the topic should be ignored.
  ///                          if not set then all topics will be read.
  /// @returns expected with error set if there was an issue opening the log
  [[nodiscard]] LogExpected<void> open(const std::function<bool(std::string_view)>& topic_filter) override;

  /// Close the log.
  /// @returns expected with error set if there was an issue closing the log
  [[nodiscard]] LogExpected<void> close() override;

  /// Get the list of channel names from the log.
  /// @return vector of channel names
  [[nodiscard]] std::vector<std::string> get_channels() override;

  /// Get the metadata for all channels in the log
  /// @return vector of topic metadata
  [[nodiscard]] std::vector<TopicMetadata> get_metadata() override;

  /// Get the metadata for a single channel
  /// @return Topic metadata or LogError on failure
  [[nodiscard]] LogExpected<TopicMetadata> get_channel_metadata(std::string_view channel_name) override;

  /// Get the log metrics
  /// @return Log metrics or LogError on failure
  [[nodiscard]] LogExpected<LogMetrics> get_metrics() override;

  /// Get the log start time
  /// @return the log start time or LogError on failure
  [[nodiscard]] LogExpected<LogTimestamp> start_time() override;

  /// Get the log end time
  /// @return the log end time or LogError on failure
  [[nodiscard]] LogExpected<LogTimestamp> end_time() override;

  /// Read the next message out of the log.
  /// @note The logged message is only valid until the next call of next_message or close.
  /// @returns next message if there are any left, otherwise nullopt
  [[nodiscard]] std::optional<LoggedMessage> next_message() override;

private:
  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;

  /// Log reader
  offboard::Reader reader_;

  /// Option for whether to decompress lite-compressed messages found in the log
  DecompressOption decompress_option_;
};

} // namespace clockwork_logging
