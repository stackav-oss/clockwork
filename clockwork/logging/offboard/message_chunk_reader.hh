// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/channel_type.hh"
#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/offboard/chunk_compressor.hh"
#include "clockwork/logging/offboard/chunk_reader.hh"
#include "clockwork/logging/offboard/log_format.hh"
#include "clockwork/logging/offboard/types.hh"
#include "clockwork/logging/wrapping_counter.hh"
#include "jewels/memory/memory_resource.hh"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace clockwork_logging::offboard
{

/// Class to read messages from message chunks
///
/// Each message chunk contains a series of logged messages for a single channel followed
/// by an index that contains the transmit timestamp and offset of each message in the
/// chunk and then a trailer that points to the start of the index entries.
///
/// The index entries are sorted by <transmit timestamp, sequence_number>.
class MessageChunkReader
{
public:
  /// Message identifier used to merge the messages from difference chunks
  struct MessageIdentifier
  {
    /// Transmission time
    LogTimestamp transmit_time{};

    /// Sequence number
    WrappingCounter<uint32_t> sequence_number{};

    /// Channel name
    std::string_view channel_name{};

    /// Less than comparison operator
    /// @param[in] lhs Left hand side
    /// @param[in] rhs Right hand side
    /// @return True if lhs < rhs
    [[nodiscard]] friend bool operator<(const MessageIdentifier& lhs, const MessageIdentifier& rhs)
    {
      return std::tie(lhs.transmit_time, lhs.sequence_number, lhs.channel_name) <
             std::tie(rhs.transmit_time, rhs.sequence_number, rhs.channel_name);
    }

    /// Equal comparison operator
    /// @param[in] lhs Left hand side
    /// @param[in] rhs Right hand side
    /// @return True if lhs == rhs
    [[nodiscard]] friend bool operator==(const MessageIdentifier& lhs, const MessageIdentifier& rhs)
    {
      return std::tie(lhs.transmit_time, lhs.sequence_number, lhs.channel_name) ==
             std::tie(rhs.transmit_time, rhs.sequence_number, rhs.channel_name);
    }

    /// Greater than comparison operator
    /// @param[in] lhs Left hand side
    /// @param[in] rhs Right hand side
    /// @return True if lhs > rhs
    [[nodiscard]] friend bool operator>(const MessageIdentifier& lhs, const MessageIdentifier& rhs)
    {
      return std::tie(lhs.transmit_time, lhs.sequence_number, lhs.channel_name) >
             std::tie(rhs.transmit_time, rhs.sequence_number, rhs.channel_name);
    }
  };

  /// Constructor
  /// @param[in] memory_resource Memory resource
  /// @param[in] channel_name Channel name
  /// @param[in] channel_type Channel type
  /// @param[in] maybe_log_interval Optional log interval
  /// @param[in] compression_type Chunk compression type
  MessageChunkReader(
    jewels::memory::MemoryResource memory_resource,
    std::string_view channel_name,
    ChannelType channel_type,
    std::optional<LogInterval> maybe_log_interval,
    CompressionType compression_type);

  ~MessageChunkReader() noexcept = default;

  MessageChunkReader(const MessageChunkReader& other) = delete;
  MessageChunkReader& operator=(const MessageChunkReader& other) = delete;
  MessageChunkReader(MessageChunkReader&&) noexcept = default;
  MessageChunkReader& operator=(MessageChunkReader&&) noexcept = default;

  /// Read a message chunk from the log file
  /// @param[in] location Chunk location
  /// @param[in] chunk_compressor Chunk compressor
  /// @param[in] chunk_reader Chunk reader
  /// @return Index entry for the written chunk or LogError on failure
  [[nodiscard]] LogExpected<void>
  read_chunk(const ChunkLocation& location, const ChunkCompressor& chunk_compressor, ChunkReader& chunk_reader);

  /// Test whether there are unread messages in the chunk
  [[nodiscard]] bool is_empty() const noexcept;

  /// Get the next message transmit time
  [[nodiscard]] LogExpected<MessageIdentifier> get_next_message_identifier() const noexcept;

  /// Get the channel type
  [[nodiscard]] ChannelType get_channel_type() const noexcept;

  /// Read the next message from the chunk
  /// @post The reader advances to the next message
  /// @return Next message from the chunk or LogError on failure
  [[nodiscard]] LogExpected<LoggedMessage> read_next();

private:
  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;

  /// Chunk channel name
  std::pmr::string channel_name_;

  /// Chunk channel type
  ChannelType channel_type_;

  /// Optional log interval
  std::optional<LogInterval> maybe_log_interval_;

  /// Compression type
  CompressionType compression_type_;

  /// Chunk data buffer
  std::pmr::vector<std::byte> data_;

  /// Span of message chunk index entries
  std::span<const MessageChunkIndexEntryV2> index_span_;

  /// Vector of message index entries used when reading old logs
  std::pmr::vector<MessageChunkIndexEntryV2> index_storage_;

  /// Index of the next message to read from the chunk
  size_t next_message_index_{};
};

} // namespace clockwork_logging::offboard
