// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/channel_type.hh"
#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/message_encoding.hh"
#include "clockwork/logging/schema_encoding.hh"

#include <wise_enum.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

namespace clockwork_logging::onboard
{

/// Trait to identify buffered reader types that read from memory
template <typename T>
struct IsMemoryBufferedReader : public std::false_type
{
};

/// Concept to identify buffered reader types that read from memory
template <typename T>
concept MemoryBufferedReaderType = IsMemoryBufferedReader<T>::value;

/// Trait to identify buffered reader types that read from disk
template <typename T>
struct IsDiskBufferedReader : public std::false_type
{
};

/// Concept to identify buffered reader types that read from disk
template <typename T>
concept DiskBufferedReaderType = IsDiskBufferedReader<T>::value;

/// Structure to contain the metadata for a logged channel
struct LoggedChannelMetadata
{
  /// Channel name
  std::string_view channel_name{};
  /// Channel compression type
  CompressionType compression_type{CompressionType::none};
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
};

/// Structure used contain a message written to a log
struct Message
{
  /// Channel name
  std::string_view channel_name;
  /// Sequence number, zero if not available
  uint32_t sequence_number;
  /// Time that the message was received by the logger
  LogTimestamp log_time;
  /// Time that the message transmitted, set to log_time if not available
  LogTimestamp message_time;
  /// Message header, should be empty if the header is not available
  std::span<const std::byte> header;
  /// Message data
  std::span<const std::byte> data;
};

/// Structure used contain a message written to a log
struct ZeroCopyMessage
{
  /// Channel name
  std::string_view channel_name;
  /// Sequence number, zero if not available
  uint32_t sequence_number;
  /// Time that the message was received by the logger
  LogTimestamp log_time;
  /// Time that the message transmitted, set to log_time if not available
  LogTimestamp message_time;
  /// Message header, should be empty if the header is not available
  std::span<const std::byte> header;
  /// Message data
  std::span<const std::span<const std::byte>> data;
};

/// Logged message type
WISE_ENUM_CLASS(
  (LoggedMessageType, uint8_t),
  /// Regular
  regular,
  /// Persistent channel message
  persistent,
  /// Persistent channel message repeated at the front of a log file
  repeated_persistent)

/// Structure used contain a message read from a log
struct LoggedMessage
{
  /// Channel name
  std::string_view channel_name;
  /// Sequence number, zero if not available
  uint32_t sequence_number;
  /// Time that the message was received by the logger
  LogTimestamp log_time;
  /// Time that the message transmitted, set to log_time if not available
  LogTimestamp message_time;
  /// Message header, should be empty if the header is not available
  std::span<const std::byte> header;
  /// Message data
  std::span<const std::byte> data;
  /// Message type
  LoggedMessageType message_type;
  /// Message encoding
  MessageEncoding message_encoding;
  /// Set to true when the message data is lite-compressed
  bool is_lite_compressed;
};

/// Structure used contain a message read from a log with zero copies.
struct ZeroCopyLoggedMessage
{
  /// Channel name
  std::string_view channel_name;
  /// Sequence number, zero if not available
  uint32_t sequence_number;
  /// Time that the message was received by the logger
  LogTimestamp log_time;
  /// Time that the message transmitted, set to log_time if not available
  LogTimestamp message_time;
  /// Message header, should be empty if the header is not available
  std::span<const std::byte> header;
  /// Message data
  std::span<const std::span<const std::byte>> data;
  /// Message type
  LoggedMessageType message_type;
  /// Message encoding
  MessageEncoding message_encoding;
  /// Set to true when the message data is lite-compressed
  bool is_lite_compressed;
};

/// Log reader error counters
struct ReaderErrorCounters
{
  /// Number of log file open failures
  size_t open_failures{};
  /// Number of invalid log headers
  size_t invalid_log_headers{};
  /// Number of errors advancing to the next record
  size_t advance_errors{};
  /// Number of invalid records found in the log
  size_t invalid_records{};
  /// Number of channels missing metadata
  size_t missing_channel_metadata{};
  /// Number of schema missing metadata
  size_t missing_schema_metadata{};
};

/// Writer runtime environment type
WISE_ENUM_CLASS(
  (WriterEnvironment, uint8_t),
  /// Normal environment
  normal,
  /// Simulation
  simulation)

/// Log reader metadata map option
WISE_ENUM_CLASS(
  (MetadataMapOption, uint8_t),
  // Disable the metadata map
  disable,
  // Enable the metadata map
  enable)

/// Log reader time filter option
WISE_ENUM_CLASS(
  (TimeFilterOption, uint8_t),
  // Filter by message time
  message_time,
  // Filter by log time
  log_time)

/// Add reader counters into a combined total
/// @param[in] counters Counters to add to the total
/// @param[in,out] total_counters Total counters
void combine_error_counters(const ReaderErrorCounters& counters, ReaderErrorCounters& total_counters);

/// Get size of a span of spans
/// @param[in] data_spans Source span of spans
/// @return Number of bytes in the span of spans
[[nodiscard]] size_t data_spans_size(std::span<const std::span<const std::byte>> data_spans);

/// Copy data out of a span of spans
/// @param[in] source_spans Source span of spans
/// @param[out] dest Destination span
void copy_data_spans(std::span<const std::span<const std::byte>> source_spans, std::span<std::byte> dest);

} // namespace clockwork_logging::onboard
