// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/message_encoding.hh"
#include "clockwork/logging/readers/abstract_log_reader.hh"
#include "clockwork/logging/readers/types.hh"
#include "jewels/memory/pointers.hh"

#include <mcap/errors.hpp>
#include <mcap/reader.hpp>
#include <mcap/types.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace clockwork_logging
{

/// Mcap reader implementation of the log reader interface
class McapLogReader : public AbstractLogReader
{
public:
  /// Constructor
  /// @param[in] log_uri Log URI
  /// @param[in] maybe_log_interval The interval to read from the log
  /// @param[in] maybe_relative_interval The interval to read from the log relative to the sart of the log
  McapLogReader(
    std::string_view log_uri,
    std::optional<LogInterval> maybe_log_interval,
    std::optional<RelativeInterval> maybe_relative_interval);

  ~McapLogReader() override;
  McapLogReader(const McapLogReader&) = delete;
  McapLogReader& operator=(const McapLogReader&) = delete;
  McapLogReader(McapLogReader&&) = delete;
  McapLogReader& operator=(McapLogReader&&) = delete;

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
  /// @return vector of channel metadata
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
  /// Open the MCAP log reader.
  /// @returns expected with error set if there was an issue opening the log
  [[nodiscard]] LogExpected<void> open_mcap_reader();

  /// Get the message encoding for a channel
  /// @param[in] channel Channel name
  /// @return Message encoding or undefined if channel is not found
  [[nodiscard]] MessageEncoding get_channel_message_encoding(std::string_view channel);

  /// Load the metadata from the underlying log reader
  void load_metadata();

  /// Convert an MCAP message view to a logged message
  /// @param[in] view MCAP message view
  /// @return Logged message
  [[nodiscard]] LoggedMessage to_logged_message(const mcap::MessageView& view);

  /// The underlying log reader
  std::unique_ptr<mcap::McapReader> reader_;
  /// Flag set when the MCAP reader is open
  bool is_mcap_reader_open_{false};
  /// Flag set when this reader is open
  bool is_open_{false};
  /// Message view into the log.
  std::unique_ptr<mcap::LinearMessageView> view_;
  /// Message view into the log.
  std::optional<mcap::LinearMessageView::Iterator> msg_iter_;
  /// Topic filter
  std::function<bool(std::string_view)> topic_filter_;
  /// Cached log metadata
  std::optional<std::vector<TopicMetadata>> maybe_metadata_;
  /// Cached channel metadata map
  std::unordered_map<std::string_view, jewels::memory::ObjectPtr<const TopicMetadata>> channel_metadata_map_;

  /// Callback registered to receive any problems while reading the log.
  void problem_callback(const mcap::Status& status);
};

} // namespace clockwork_logging
