// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/channel_type_clk_cc.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/message_encoding_clk_cc.hh"
#include "clockwork/logging/schema_encoding_clk_cc.hh"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace clockwork_logging
{

/// Interval relative to the start of the log
struct RelativeInterval
{
  /// Interval start relative to the start of the log
  std::chrono::nanoseconds start_offset{std::chrono::nanoseconds{0}};

  /// Interval end relative to the start of the log
  std::chrono::nanoseconds end_offset{std::chrono::nanoseconds{std::numeric_limits<int64_t>::max()}};

  /// Equals operator
  bool operator==(const RelativeInterval& lhs) const = default;
};

/// Log Reader Configuration
struct LogReaderConfig
{
  // The identifier of the log to read.
  std::string uri;
  // The interval to read from the log.
  std::optional<LogInterval> interval{};
  // The interval to read from the log relative to start of the log
  std::optional<RelativeInterval> relative_interval{};
  // Optional topic filter, return false if the topic should be ignored.
  // if not set then all topics will be read.
  std::function<bool(std::string_view)> topic_filter{};
  // Optional channel and sequence number filter. Return false to skip a message.
  std::function<bool(std::string_view, uint32_t)> sequence_number_filter{};
};

/// Topic metadata
struct TopicMetadata
{
  /// Name of the topic
  std::string name;
  /// The message type
  std::string type;
  /// Message encoding
  MessageEncoding message_encoding;
  /// Channel type
  ChannelType channel_type;
  /// Schema encoding
  SchemaEncoding schema_encoding;
  /// Schema definition
  std::string schema_definition;
  /// Set to true when channel is amended
  bool is_amended;

  /// Comparison operator
  bool operator<=>(const TopicMetadata&) const = default;
};

/// Logged message data
/// The members are only valid iterator or callback.
struct LoggedMessage
{
  /// The topic the message was logged on.
  std::string_view topic;
  /// Sequence number
  uint32_t sequence_number{};
  /// The publish time of the message.
  LogTimestamp publish_time;
  /// The log time of the message.
  LogTimestamp log_time;
  /// Span of the raw, serialized message header.
  std::span<const std::byte> header;
  /// Span of the raw, serialized message data.
  std::span<const std::byte> data;
  /// Flag indicating a repeated persistent message published prior to the start of the log
  bool is_repeated_persistent{};
  /// Message encoding
  MessageEncoding message_encoding{};
  /// Flag set when the serialized message is lite compressed
  bool is_lite_compressed{};

  /// Equals operator
  friend bool operator==(const LoggedMessage& lhs, const LoggedMessage& rhs)
  {
    return std::tie(
             lhs.topic,
             lhs.sequence_number,
             lhs.publish_time,
             lhs.log_time,
             lhs.is_repeated_persistent,
             lhs.message_encoding,
             lhs.is_lite_compressed) ==
             std::tie(
               rhs.topic,
               rhs.sequence_number,
               rhs.publish_time,
               rhs.log_time,
               rhs.is_repeated_persistent,
               rhs.message_encoding,
               rhs.is_lite_compressed) &&
           std::ranges::equal(lhs.header, rhs.header) && std::ranges::equal(lhs.data, rhs.data);
  }
};

/// Zero copy logged message data
/// The members are only valid iterator or callback.
struct ZeroCopyLoggedMessage
{
  /// The topic the message was logged on.
  std::string_view topic;
  /// Sequence number
  uint32_t sequence_number{};
  /// The publish time of the message.
  LogTimestamp publish_time;
  /// The log time of the message.
  LogTimestamp log_time;
  /// Span of the raw, serialized message header.
  std::span<const std::byte> header;
  /// Spans containing the raw, serialized message data.
  std::span<const std::span<const std::byte>> data;
  /// Flag indicating a repeated persistent message published prior to the start of the log
  bool is_repeated_persistent{};
  /// Message encoding
  MessageEncoding message_encoding{};
  /// Flag set when the serialized message is lite compressed
  bool is_lite_compressed{};
};

/// Logged topic metrics
struct LoggedTopicMetrics
{
  /// Topic name
  std::string topic;
  /// Transmit time interval
  LogInterval transmit_time_interval;
  /// Message count
  uint64_t message_count{};
  /// Byte count
  uint64_t byte_count{};

  /// Equals operator
  bool operator==(const LoggedTopicMetrics&) const = default;
};

/// Log metrics
struct LogMetrics
{
  /// Transmit time interval
  LogInterval transmit_time_interval;
  /// Message count
  uint64_t message_count{};
  /// Byte count
  uint64_t byte_count{};
  /// Logged topic metrics
  std::vector<LoggedTopicMetrics> topic_metrics;

  /// Equals operator
  bool operator==(const LogMetrics&) const = default;
};

} // namespace clockwork_logging
