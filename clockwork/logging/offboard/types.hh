// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/channel_type.hh"
#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/message_encoding.hh"
#include "clockwork/logging/schema_encoding.hh"

#include <wise_enum.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory_resource>
#include <span>
#include <string_view>
#include <vector>

namespace clockwork_logging::offboard
{

/// Structure to contain the metadata for a logged channel
struct LoggedChannelMetadata
{
  /// Channel name
  std::string_view channel_name{};
  /// Message encoding
  MessageEncoding message_encoding{MessageEncoding::undefined};
  /// Channel type
  ChannelType channel_type{ChannelType::regular};
  /// Schema name, should be empty if message encoding is undefined
  std::string_view schema_name{};
  /// Schema encoding, should be undefined if the message encoding is undefined
  SchemaEncoding schema_encoding{SchemaEncoding::undefined};
  /// Schema definition string, should be empty if schema encoding is undefined
  std::string_view schema_definition{};

  /// Comparison operator
  /// @param[in] lhs Left hand operand
  /// @param[in] rhs Right hand operand
  /// @return True iff lhs == rhs
  [[nodiscard]] friend bool operator==(const LoggedChannelMetadata& lhs, const LoggedChannelMetadata& rhs) noexcept
  {
    return std::tie(
             lhs.channel_name,
             lhs.message_encoding,
             lhs.channel_type,
             lhs.schema_name,
             lhs.schema_encoding,
             lhs.schema_definition) ==
           std::tie(
             rhs.channel_name,
             rhs.message_encoding,
             rhs.channel_type,
             rhs.schema_name,
             rhs.schema_encoding,
             rhs.schema_definition);
  }
};

/// Structure used contain a logged message
struct LoggedMessage
{
  /// Channel name
  std::string_view channel_name{};
  /// Sequence number, zero if not available
  uint32_t sequence_number{};
  /// Time that the message was received by the logger
  LogTimestamp log_time{};
  /// Time that the message transmitted, set to log_time if not available
  LogTimestamp transmit_time{};
  /// Message header, should be empty if the header is not available
  std::span<const std::byte> header{};
  /// Message data
  std::span<const std::byte> data{};
  /// Flag indicating that the message is a repeated persistent channel message from a prior log
  bool is_repeated_persistent{};
  /// Message encoding
  MessageEncoding message_encoding = MessageEncoding::unspecified;
  /// Flag indicating that the message is compressed with lite compression
  bool is_lite_compressed{};
};

/// Structure used contain a logged message for zero copy writes
struct ZeroCopyLoggedMessage
{
  /// Channel name
  std::string_view channel_name{};
  /// Sequence number, zero if not available
  uint32_t sequence_number{};
  /// Time that the message was received by the logger
  LogTimestamp log_time{};
  /// Time that the message transmitted, set to log_time if not available
  LogTimestamp transmit_time{};
  /// Message header, should be empty if the header is not available
  std::span<const std::byte> header{};
  /// Message data
  std::span<const std::span<const std::byte>> data{};
  /// Flag indicating that the message is a repeated persistent channel message from a prior log
  bool is_repeated_persistent{};
  /// Message encoding
  MessageEncoding message_encoding = MessageEncoding::unspecified;
  /// Flag indicating that the message is compressed with lite compression
  bool is_lite_compressed{};
};

/// Structure to contain the metrics for a logged channel
struct LoggedChannelMetrics
{
  /// Message count
  uint32_t message_count{};
  /// Byte count
  uint64_t byte_count{};
  /// Transmit time interval
  LogInterval transmit_time_interval{};
};

/// Structure to contain the metrics for a log
struct LogMetrics
{
  /// Message count
  uint32_t message_count{};
  /// Byte count
  uint64_t byte_count{};
  /// Transmit time interval
  LogInterval transmit_time_interval;
  /// Map from channel name to channel metrics
  std::pmr::map<std::string_view, LoggedChannelMetrics> metrics_map;
};

/// Message chunk index format
WISE_ENUM_CLASS((MessageChunkIndexFormat, uint8_t), v1, v2)

} // namespace clockwork_logging::offboard
