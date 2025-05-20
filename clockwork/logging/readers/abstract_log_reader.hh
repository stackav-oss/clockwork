// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/readers/types.hh"

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace clockwork_logging
{

/// Virtual log reader interface interface. This is the low level log reading
/// interface that will be implemented for each log format thats supported.
class AbstractLogReader
{
public:
  /// Constructor
  /// @param[in] log_uri Log URI
  /// @param[in] maybe_log_interval The interval to read from the log
  /// @param[in] maybe_relative_interval The interval to read from the log relative to the sart of the log
  AbstractLogReader(
    std::string_view log_uri,
    std::optional<LogInterval> maybe_log_interval,
    std::optional<RelativeInterval> maybe_relative_interval);

  virtual ~AbstractLogReader();
  AbstractLogReader(const AbstractLogReader&) = delete;
  AbstractLogReader& operator=(const AbstractLogReader&) = delete;
  AbstractLogReader(AbstractLogReader&&) = delete;
  AbstractLogReader& operator=(AbstractLogReader&&) = delete;

  /// Get the log URI
  /// @returns Log URI
  [[nodiscard]] std::string_view log_uri() const;

  /// Get the interval to read from the klog
  /// @returns Interval to read from the log
  [[nodiscard]] std::optional<LogInterval> maybe_log_interval() const;

  /// Get the relative interval to read from the klog
  /// @returns Interval to read from the log relative to the start of the log
  [[nodiscard]] std::optional<RelativeInterval> maybe_relative_interval() const;

  /// Get the type of log reader
  /// @returns The log reader type
  [[nodiscard]] virtual std::string type() const = 0;

  /// Open the log.
  /// @param[in] topic_filter Optional topic filter, return false if the topic should be ignored.
  ///                          if not set then all topics will be read.
  /// @returns expected with error set if there was an issue opening the log
  [[nodiscard]] virtual LogExpected<void> open(const std::function<bool(std::string_view)>& topic_filter) = 0;

  /// Close the log.
  /// @returns expected with error set if there was an issue closing the log
  [[nodiscard]] virtual LogExpected<void> close() = 0;

  /// Get the list of channel names in the log.
  /// @returns Vector of channel names
  [[nodiscard]] virtual std::vector<std::string> get_channels() = 0;

  /// Get the metadata for all channels
  /// @returns Vector of topic metadata
  [[nodiscard]] virtual std::vector<TopicMetadata> get_metadata() = 0;

  /// Get the metadata for a single channel
  /// @param[in] channel_name Channel name
  /// @returns Topic metadata or LogError on failure
  [[nodiscard]] virtual LogExpected<TopicMetadata> get_channel_metadata(std::string_view channel_name) = 0;

  /// Get the log metrics
  /// @note The default implementation not_implemented
  /// @return Log metrics or LogError on failure
  [[nodiscard]] virtual LogExpected<LogMetrics> get_metrics();

  /// Get the log start time
  /// @return the log start time or LogError on failure
  [[nodiscard]] virtual LogExpected<LogTimestamp> start_time() = 0;

  /// Get the log end time
  /// @return the log end time or LogError on failure
  [[nodiscard]] virtual LogExpected<LogTimestamp> end_time() = 0;

  /// Read the next message out of the log.
  /// @note The logged message is only valid until the next call of next_message or close.
  /// @returns next message if there are any left, otherwise nullopt
  [[nodiscard]] virtual std::optional<LoggedMessage> next_message() = 0;

  /// Read the next message out of the log with zero copy.
  /// @note The logged message is only valid until the next call of next_message or close.
  /// @returns next message if there are any left, otherwise nullopt
  [[nodiscard]] virtual std::optional<ZeroCopyLoggedMessage> zero_copy_next_message();

private:
  /// Log URI
  std::string log_uri_;

  /// The interval to read from the log
  std::optional<LogInterval> maybe_log_interval_;

  /// The interval to read from the log relative to the sart of the log
  std::optional<RelativeInterval> maybe_relative_interval_;

  /// Storage for zero copy data span for default zero_copy_read_next implementation
  std::span<const std::byte> zero_copy_data_span_{};
};

} // namespace clockwork_logging
