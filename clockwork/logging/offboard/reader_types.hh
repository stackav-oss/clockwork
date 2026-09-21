// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/channel_type_clk_cc.hh"
#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/message_encoding_clk_cc.hh"
#include "clockwork/logging/offboard/chunk_compressor.hh"
#include "clockwork/logging/offboard/chunk_reader.hh"
#include "clockwork/logging/offboard/log_format.hh"
#include "clockwork/logging/schema_encoding_clk_cc.hh"

#include <compare>
#include <cstdint>
#include <functional>
#include <memory>
#include <memory_resource>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>

namespace clockwork_logging::offboard::reader
{

/// Information about a channel in a log file
struct LoggedChannelInfo
{
  /// Compression type
  CompressionType compression_type{};

  /// Channel name
  std::pmr::string channel_name{};

  /// Message encoding
  MessageEncoding message_encoding{MessageEncoding::undefined};

  /// Channel type
  ChannelType channel_type{ChannelType::regular};

  /// Schema name, should be empty if message encoding is undefined
  std::pmr::string schema_name{};

  /// Schema encoding
  SchemaEncoding schema_encoding{SchemaEncoding::undefined};

  /// Schema definition string
  std::pmr::string schema_definition{};

  /// Channel is amended
  bool is_amended{};
};

/// Metrics for a channel in a log file
struct LoggedChannelMetrics
{
  /// Number of messages written
  uint32_t message_count{};

  /// Number of bytes written
  uint64_t byte_count{};

  /// Time interval for the messages in the channel
  LogInterval transmit_time_interval;
};

/// Log metrics
struct LogMetrics
{
  /// Number of messages written
  uint32_t message_count{};

  /// Number of bytes written
  uint64_t byte_count{};

  /// Time interval for the messages in the log
  LogInterval transmit_time_interval;

  /// Map containing the metrics for each logged channel
  std::pmr::unordered_map<std::pmr::string, LoggedChannelMetrics> metrics_map;
};

struct ChunkHandle
{
  /// Compression type
  CompressionType compression_type{};

  /// Location of the chunk in the log file
  ChunkLocation location{};

  /// Chunk reader pointer used to read the chunk
  std::shared_ptr<ChunkReader> chunk_reader_ptr;

  /// Chunk compressor pointer used to decompress the chunk
  std::shared_ptr<ChunkCompressor> chunk_compressor_ptr;
};

/// Handle for a message chunk to be read from the log
struct MessageChunkHandle
{
  /// Chunk handle
  ChunkHandle chunk_handle{};

  /// Channel name
  std::string_view channel_name;

  /// Channel type
  ChannelType channel_type{};

  /// Minimum transmit time stored in the chunk
  LogTimestamp min_transmit_time{};

  /// Optional time range to read from the chunk
  std::optional<LogInterval> maybe_log_interval;

  /// Comparison operator used to sort chunk handles by read order
  /// @param[in] lhs Left hand operand
  /// @param[in] rhs Right hand operand
  /// @return True iff lhs should be read before rhs
  [[nodiscard]] friend bool operator<(const MessageChunkHandle& lhs, const MessageChunkHandle& rhs) noexcept
  {
    return (lhs.min_transmit_time == rhs.min_transmit_time && lhs.channel_type == ChannelType::persistent &&
            rhs.channel_type == ChannelType::regular) ||
           std::tie(lhs.min_transmit_time, lhs.channel_name) < std::tie(rhs.min_transmit_time, lhs.channel_name);
  }
};

/// Log file trailer information
struct LogFileTrailerInfo
{
  /// Metadata chunk handle
  ChunkHandle metadata_chunk_handle{};

  /// Metrics chunk handle
  ChunkHandle metrics_chunk_handle{};

  /// Index chunk handle
  ChunkHandle index_chunk_handle{};
};

} // namespace clockwork_logging::offboard::reader
