// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/readers/abstract_log_reader.hh"
#include "clockwork/logging/readers/log_reader.hh"
#include "clockwork/logging/readers/types.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/std/expected.hh"

#include <wise_enum.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace clockwork_logging
{

/// Behavior for deserialization errors
WISE_ENUM_CLASS(
  (DeserializationErrorBehavior, uint8_t),
  // Keep reading other channels, ignoring the channel that encountered the error
  keep_going,
  // Throw an exception
  throw_exception)

/// Callback based log reader/processor for clockwork logs.
class LogProcessor
{
public:
  using LoggedMessageCallback = std::function<void(const LoggedMessage&)>;

  /// Constructor
  /// @param[in] config The log reader configuration
  explicit LogProcessor(const LogReaderConfig& config);

  /// Constructor
  /// @param[in] reader Log reader
  /// @param[in] topic_filter Optional topic filter, if not set all topics are read
  LogProcessor(std::unique_ptr<AbstractLogReader> reader, std::function<bool(std::string_view)> topic_filter);

  ~LogProcessor();
  LogProcessor(const LogProcessor&) = delete;
  LogProcessor& operator=(const LogProcessor&) = delete;
  LogProcessor(LogProcessor&&) = delete;
  LogProcessor& operator=(LogProcessor&&) = delete;

  /// Add a callback to receive the deserialized message tachyon message.
  /// @tparam T Message type
  /// @param[in] topic The topic to add the callback to.
  /// @param[in] callback The callback to invoke on new messages.
  /// @return Reference to this class.
  template <typename T>
  LogProcessor& add_tappy_callback(
    std::string_view topic,
    std::function<void(const T&)> callback,
    DeserializationErrorBehavior error_behavior = DeserializationErrorBehavior::throw_exception)
    requires clockwork::TappyType<T>;

  /// Add a callback to receive the deserialized tachyon message with timestamp.
  /// @tparam T Message type
  /// @param[in] topic The topic to add the callback to.
  /// @param[in] callback The callback to invoke on new messages.
  /// @return Reference to this class.
  template <typename T>
  LogProcessor& add_tappy_callback(
    std::string_view topic,
    std::function<void(const LogTimestamp& publish_time, const T&)> callback,
    DeserializationErrorBehavior error_behavior = DeserializationErrorBehavior::throw_exception)
    requires clockwork::TappyType<T>;

  /// Add a callback to receive the raw LoggedMessage.
  /// @param[in] topic The topic to add the callback to.
  /// @param[in] callback The callback to invoke on new messages.
  /// @return Reference to this class.
  LogProcessor& add_raw_msg_callback(std::string_view topic, LoggedMessageCallback callback);

  /// Process the log. Read all messages out of the log and invoke the callbacks as needed.
  /// @return if log is read completely or not.
  bool process();

  // Return the relevant topic messages from the log one at a time.
  // DO NOT use this interface unless you have to. The pitfall is that the return value
  // is a view onto a memory. The memory may be invalidated the second time next is called.
  // If you are using this API, you should read from the memory immediately before calling next again.
  //
  // You should likely stick to the process interface.
  // @return the logged message or empty if log is already read.
  std::optional<LoggedMessage> next();

  /// Helper to generate the new topic filter based on registered callbacks.
  /// @param[in] filter The original topic_filter.
  /// @param[in] topics Iterable topic list.
  /// @param T An iterable type containing strings.
  /// @return New topic filter augmented to also filter any topics in the input list.
  template <typename T>
  [[nodiscard]] static std::function<bool(std::string_view)>
  augment_topic_filter(std::function<bool(std::string_view)> filter, const T& topics);

  /// Get the list of topics from the log.
  /// @return vector of topic metadata
  [[nodiscard]] std::vector<TopicMetadata> topics();

  /// Get the metadata for a topic from the log
  /// @param[in] topic Topic name
  /// @return Topic metadata or MonoError if the topic metadata is not in the log
  [[nodiscard]] jewels::expected<TopicMetadata, jewels::MonoError> try_get_topic_metadata(const std::string& topic);

  /// Abort log processing, can be resumed later by calling process() starting from the next message.
  ///
  /// This is safe to call from within a callback.
  void abort();

  /// Get the log metrics
  /// @return LogMetrics or LogError on failure
  [[nodiscard]] LogExpected<LogMetrics> get_metrics();

  /// Get the log start time
  /// @return the log start time or LogError on failure
  [[nodiscard]] LogExpected<LogTimestamp> start_time();

  /// Get the log end time
  /// @return the log end time or LogError on failure
  [[nodiscard]] LogExpected<LogTimestamp> end_time();

protected:
  /// Log URI accessor
  /// @return The log URI string
  [[nodiscard]] std::string_view log_uri() const;

  /// Reader accessor
  /// @return The log reader
  [[nodiscard]] LogReader& reader();

  /// Failed topic set accessor
  /// @return The failed topic set
  [[nodiscard]] std::unordered_set<std::string>& failed_topics();

private:
  /// Log URI
  std::string log_uri_;
  /// Log reader
  std::unique_ptr<LogReader> reader_;
  /// Topic filter, if not set all topics are read
  std::function<bool(std::string_view)> topic_filter_;
  /// Map of callbacks registered per topic
  std::map<std::string, std::vector<LoggedMessageCallback>> callbacks_;
  /// Flag set when processing has been stopped
  bool abort_flag_{false};
  /// Map from topic name to topic metadata
  std::optional<std::unordered_map<std::string, TopicMetadata>> maybe_topic_map_;
  /// Current message
  std::optional<LogReader::Iterator> current_message_iterator_;
  /// Set of topics that have failed to to deserialization errors
  std::unordered_set<std::string> failed_topics_;
};

} // namespace clockwork_logging

#include "clockwork/logging/readers/log_processor.inl"
