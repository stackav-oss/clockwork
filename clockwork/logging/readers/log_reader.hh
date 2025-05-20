// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/readers/abstract_log_reader.hh"
#include "clockwork/logging/readers/types.hh"

#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <vector>

namespace clockwork_logging
{

/// Iterator based log reader.
class LogReader
{
public:
  /// Log message iterator
  class Iterator
  {
  public:
    using iterator_category = std::input_iterator_tag;
    using difference_type = ssize_t;
    using value_type = LoggedMessage;
    using pointer = const LoggedMessage*;
    using reference = const LoggedMessage&;

    /// Constructor
    Iterator();
    explicit Iterator(AbstractLogReader* reader);
    /// Constructor
    /// @param[in] reader The log reader
    /// @param[in] msg Logged message instance
    Iterator(AbstractLogReader* reader, std::optional<LoggedMessage> msg);

    /// Dereference
    /// @pre Iterator is not end iterator
    reference operator*() const;
    /// Dereference
    /// @pre Iterator is not end iterator
    pointer operator->() const;
    /// Pre-increment
    Iterator& operator++();
    /// Post-increment
    /// @note The return type is void because the iterators are not valid after incrementing.
    void operator++(int);
    /// Equality operator.
    /// @param[in] lhs Left hand side value
    /// @param[in] rhs Right hand side value
    /// @return true if the iterators are equal
    friend bool operator==(const Iterator& lhs, const Iterator& rhs) = default;
    /// Inequality operator.
    /// @param[in] lhs Left hand side value
    /// @param[in] rhs Right hand side value
    /// @return true if the iterators are not equal
    friend bool operator!=(const Iterator& lhs, const Iterator& rhs) = default;

  private:
    /// Pointer to the actual log reader
    AbstractLogReader* reader_ = nullptr;
    /// Pointer to the actual log reader
    std::optional<LoggedMessage> msg_ = {};
  };

  using iterator = Iterator;

  /// Constructor
  /// @param[in] log_uri Log URI
  /// @param[in] maybe_log_interval The interval to read from the log
  /// @param[in] maybe_relative_interval The interval to read from the log relative to the sart of the log
  explicit LogReader(
    std::string_view log_uri,
    std::optional<LogInterval> maybe_log_interval,
    std::optional<RelativeInterval> maybe_relative_interval);

  /// Constructor
  /// @param[in] reader Log reader
  explicit LogReader(std::unique_ptr<AbstractLogReader> reader);

  ~LogReader();
  LogReader(const LogReader&) = delete;
  LogReader& operator=(const LogReader&) = delete;
  LogReader(LogReader&&) = delete;
  LogReader& operator=(LogReader&&) = delete;

  /// Get the log URI used to construct the reader.
  [[nodiscard]] std::string_view log_uri() const;

  /// Get the log interval used to construct the reader.
  [[nodiscard]] std::optional<LogInterval> maybe_log_interval() const;

  /// Get the relative interval used to construct the reader.
  [[nodiscard]] std::optional<RelativeInterval> maybe_relative_interval() const;

  /// Get the list of channel names from the log.
  /// @return vector of channel names
  [[nodiscard]] std::vector<std::string> get_channels();

  /// Get the list of topic metadata from the log.
  /// @return vector of topic metadata
  [[nodiscard]] std::vector<TopicMetadata> get_metadata();

  /// Get the metadata for a single channel
  /// @return Topic metadata or LogError on failure
  [[nodiscard]] LogExpected<TopicMetadata> get_channel_metadata(std::string_view channel_name);

  /// Get the log metrics
  /// @return Log metrics or LogError on failure
  [[nodiscard]] LogExpected<LogMetrics> get_metrics();

  /// Get the log start time
  /// @return the log start time or LogError on failure
  [[nodiscard]] LogExpected<LogTimestamp> start_time();

  /// Get the log end time
  /// @return the log end time or LogError on failure
  [[nodiscard]] LogExpected<LogTimestamp> end_time();

  /// Open the log.
  /// @param[in] topic_filter Optional topic filter, return false if the topic should be ignored.
  ///                          if not set then all topics will be read.
  /// @returns expected with error set if there was an issue opening the log
  [[nodiscard]] LogExpected<void> open(const std::function<bool(std::string_view)>& topic_filter);

  /// The begin message iterator.
  /// @throws runtime_error if the log has not been opened
  [[nodiscard]] iterator begin();
  /// The end message iterator.
  [[nodiscard]] iterator end();

private:
  /// The underlying log reader.
  std::unique_ptr<AbstractLogReader> reader_;
  /// Flag indicating if the reader has been opened successfully.
  bool opened_ = false;
};

} // namespace clockwork_logging
