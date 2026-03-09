// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/channel_type_clk_cc.hh"
#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/message_encoding_clk_cc.hh"
#include "clockwork/logging/schema_encoding_clk_cc.hh"
#include "clockwork/logging/wrapping_counter.hh"
#include "jewels/math/constants.hh"

#include <string_view>

namespace clockwork_logging::offboard
{

/// Maximum message data size
static constexpr size_t max_message_data_size = 640U * jewels::math::constants::bytes_per_mib<size_t>;

/// Maximum message header size
static constexpr size_t max_message_header_size = std::numeric_limits<uint16_t>::max();

/// Target file chunk size in bytes
static constexpr size_t target_file_chunk_size = 10U * jewels::math::constants::bytes_per_mib<size_t>;

/// Maximum file chunk size in bytes
static constexpr size_t max_file_chunk_size = 68U * jewels::math::constants::bytes_per_mib<size_t>;

/// Log file name suffix
static constexpr auto log_file_suffix = std::string_view{".slog"};

/// Log metadata file name
static constexpr auto log_metadata_filename = std::string_view{"stack_log_metadata.pbtxt"};

/// Log union file name
static constexpr auto log_union_filename = std::string_view{"stack_log_union.pbtxt"};

/// Maximum schema or channel name string length
static constexpr size_t max_name_string_size = std::numeric_limits<uint16_t>::max();

/// Maximum schema definition string size
static constexpr auto max_schema_definition_string_size = 256U * jewels::math::constants::bytes_per_kib<size_t>;

/// Log file magic number
static constexpr std::array log_file_magic_number = {
  std::byte{'S'},
  std::byte{'T'},
  std::byte{'A'},
  std::byte{'C'},
  std::byte{'K'},
  std::byte{'L'},
  std::byte{'O'},
  std::byte{'G'}};

/// Location of a chunk in the log file
struct __attribute__((packed)) ChunkLocation
{
  /// File offset to the referenced chunk
  uint64_t chunk_offset{};

  /// Referenced chunk size in bytes
  uint32_t chunk_size{};
};

/// Trailer common to all chunk trailer records
struct __attribute__((packed)) ChunkTrailerCommon
{
  /// Compression type
  CompressionType compression_type{CompressionType::none};

  /// Reserved bytes (set to zero)
  static constexpr auto reserved_size = 2U;
  std::array<std::byte, reserved_size> reserved{};

  /// Chunk checksum, set to zero of compression type is not 'none'
  uint64_t checksum{};
};

static constexpr size_t chunk_trailer_common_size = 12U;
static_assert(sizeof(ChunkTrailerCommon) == chunk_trailer_common_size); // Common chunk trailer size must never change

/// Log file trailer chunk
struct __attribute__((packed)) LogFileTrailerChunk
{
  /// Index chunk location
  ChunkLocation index_chunk_location{};

  /// Metrics chunk location
  ChunkLocation metrics_chunk_location{};

  /// Metadata chunk location
  ChunkLocation metadata_chunk_location{};

  /// Reserved (set to zero)
  static constexpr auto reserved_size = 8U;
  std::array<std::byte, reserved_size> reserved{};

  /// Log file magic number
  std::array<std::byte, log_file_magic_number.size()> magic_number{log_file_magic_number};

  /// Common chunk trailer
  ChunkTrailerCommon common_trailer{};
};

/// Message chunk trailer flags
struct __attribute__((packed)) MessageChunkTrailerFlags
{
  /// Chunk uses version 2 index entry format
  uint8_t use_message_index_version_2 : 1 {};

  /// Reserved bits must be zero
  uint8_t reserved : 7 {};
};

static constexpr size_t log_file_trailer_chunk_size = 64U;
static_assert(sizeof(LogFileTrailerChunk) == log_file_trailer_chunk_size); // Trailer chunk size must never change

/// Message chunk trailer
struct __attribute__((packed)) MessageChunkTrailer
{
  /// Chunk offset to the start of the index section
  uint32_t index_offset{};

  /// Message chunk trailer flags
  MessageChunkTrailerFlags flags{};

  /// Reserved bytes (set to zero)
  static constexpr auto reserved_size = 3U;
  std::array<std::byte, reserved_size> reserved{};

  /// Common chunk trailer
  ChunkTrailerCommon common_trailer{};
};

static constexpr size_t message_chunk_trailer_size = 20U;
static_assert(
  sizeof(MessageChunkTrailer) == message_chunk_trailer_size); // Message chunk trailer size must never change

/// Message chunk index entry version 1 (deprecated)
struct __attribute__((packed)) MessageChunkIndexEntryV1
{
  /// Message transmit timestamp in nanoseconds since the start of the epoch
  int64_t transmit_time_ns{};

  /// Message chunk offset
  uint32_t chunk_offset{};

  /// Comparison operator
  /// @param[in] lhs Left hand operand
  /// @param[in] rhs Right hand operand
  /// @return True iff lhs < rhs
  [[nodiscard]] bool friend operator<(const MessageChunkIndexEntryV1& lhs, const MessageChunkIndexEntryV1& rhs) noexcept
  {
    return (lhs.transmit_time_ns < rhs.transmit_time_ns) ||
           ((lhs.transmit_time_ns == rhs.transmit_time_ns) && (lhs.chunk_offset < rhs.chunk_offset));
  }
};

static constexpr size_t message_chunk_index_entry_v1_size = 12U;
static_assert(
  sizeof(MessageChunkIndexEntryV1) ==
  message_chunk_index_entry_v1_size); // Message chunk index entry size must never change

/// Message chunk index entry version 2
struct __attribute__((packed)) MessageChunkIndexEntryV2
{
  /// Message transmit timestamp in nanoseconds since the start of the epoch
  int64_t transmit_time_ns{};

  /// Message sequence number
  WrappingCounter<uint32_t> sequence_number{};

  /// Message chunk offset
  uint32_t chunk_offset{};

  /// Comparison operator
  /// @param[in] lhs Left hand operand
  /// @param[in] rhs Right hand operand
  /// @return True iff lhs < rhs
  [[nodiscard]] bool friend operator<(const MessageChunkIndexEntryV2& lhs, const MessageChunkIndexEntryV2& rhs) noexcept
  {
    return (lhs.transmit_time_ns < rhs.transmit_time_ns) ||
           ((lhs.transmit_time_ns == rhs.transmit_time_ns) && (lhs.sequence_number < rhs.sequence_number)) ||
           ((lhs.transmit_time_ns == rhs.transmit_time_ns) && (lhs.sequence_number == rhs.sequence_number) &&
            (lhs.chunk_offset < rhs.chunk_offset));
  }
};

static constexpr size_t message_chunk_index_entry_v2_size = 16U;
static_assert(
  sizeof(MessageChunkIndexEntryV2) ==
  message_chunk_index_entry_v2_size); // Message chunk index entry size must never change

/// Message header flags
struct __attribute__((packed)) MessageChunkMessageHeaderFlags
{
  /// Message is a repeated persistent message from a prior log file
  uint8_t is_repeated_persistent : 1 {};

  /// Message is lite compressed
  uint8_t is_lite_compressed : 1 {};

  /// Reserved bits must be zero
  uint8_t reserved : 6 {};
};

/// Logged message header
struct __attribute__((packed)) MessageChunkMessageHeader
{
  /// Message data size in bytes
  uint32_t data_size{0U};

  /// Message header size in bytes
  uint16_t header_size{0U};

  /// Flags field
  MessageChunkMessageHeaderFlags flags{};

  /// Reserved bytes (set to zero)
  static constexpr auto reserved_size = 1U;
  std::array<std::byte, 1U> reserved{};

  /// Sequence number, set to zero if not available
  uint32_t sequence_number{0U};

  /// Log time in nanoseconds since the start of the unix epoch
  int64_t log_time_ns{0};

  /// Transmit time in nanoseconds since the start of the unix epoch, set to log_time if not available
  int64_t transmit_time_ns{0};
};

static constexpr size_t message_chunk_message_header_size = 28U;
static_assert(
  sizeof(MessageChunkMessageHeader) ==
  message_chunk_message_header_size); // Message chunk trailer size must never change

/// Index chunk trailer
struct __attribute__((packed)) IndexChunkTrailer
{
  /// Chunk offset to the start of the channel entries section
  uint32_t channel_entries_offset{};

  /// Reserved bytes (set to zero)
  static constexpr auto reserved_size = 4U;
  std::array<std::byte, 4U> reserved{};

  /// Common chunk trailer
  ChunkTrailerCommon common_trailer{};
};

static constexpr size_t index_chunk_trailer_size = 20U;
static_assert(sizeof(IndexChunkTrailer) == index_chunk_trailer_size); // Index chunk trailer size must never change

/// Index chunk channel entry
struct __attribute__((packed)) IndexChunkChannelEntry
{
  /// Channel ID
  uint16_t channel_id{};

  /// Reserved bytes (set to zero)
  static constexpr auto reserved_size = 2U;
  std::array<std::byte, 2U> reserved{};

  /// Chunk offset to the start of the channel index
  uint32_t channel_index_offset{};

  /// Number of entries in the channel index
  uint32_t channel_index_size{};
};

static constexpr size_t index_chunk_channel_entry_size = 12U;
static_assert(
  sizeof(IndexChunkChannelEntry) == index_chunk_channel_entry_size); // Index chunk channel entry size must never change

/// Index chunk index entry
struct __attribute__((packed)) IndexChunkIndexEntry
{
  /// Earliest transmit time stored in the referenced chunk
  int64_t min_transmit_time_ns{};

  /// Latest transmit time stored in the referenced chunk
  int64_t max_transmit_time_ns{};

  /// Location of the referenced chunk
  ChunkLocation location{};

  /// Reserved bytes (set to zero)
  static constexpr auto reserved_size = 4U;
  std::array<std::byte, 4U> reserved{};

  /// Comparison operator
  /// @param[in] lhs Left hand operand
  /// @param[in] rhs Right hand operand
  /// @return True iff lhs.min_transmit_time_ns < rhs.min_transmit_time_ns
  [[nodiscard]] bool friend operator<(const IndexChunkIndexEntry& lhs, const IndexChunkIndexEntry& rhs) noexcept
  {
    return lhs.min_transmit_time_ns < rhs.min_transmit_time_ns;
  }
};

static constexpr size_t index_chunk_index_entry_size = 32U;
static_assert(
  sizeof(IndexChunkIndexEntry) == index_chunk_index_entry_size); // Index chunk index entry size must never change

/// Metrics chunk trailer
struct __attribute__((packed)) MetricsChunkTrailer
{
  /// Chunk offset to the start of the channel entries section
  uint32_t channel_entries_offset{};

  /// Reserved bytes (set to zero)
  static constexpr auto reserved_size = 4U;
  std::array<std::byte, 4U> reserved{};

  /// Common chunk trailer
  ChunkTrailerCommon common_trailer{};
};

static constexpr size_t metrics_chunk_trailer_size = 20U;
static_assert(
  sizeof(MetricsChunkTrailer) == metrics_chunk_trailer_size); // Metrics chunk trailer size must never change

/// Metrics chunk channel entry
struct __attribute__((packed)) MetricsChunkChannelEntry
{
  /// Channel ID
  uint16_t channel_id{};

  /// Number of messages written to the log file
  uint32_t message_count{};

  /// Number of bytes written to the log file
  uint64_t byte_count{};

  /// Minimum message transmit time in nanoseconds
  int64_t min_transmit_time_ns{std::numeric_limits<int64_t>::max()};

  /// Maximum message transmit time in nanoseconds
  int64_t max_transmit_time_ns{std::numeric_limits<int64_t>::min()};

  /// Reserved bytes (set to zero)
  static constexpr auto reserved_size = 64U;
  std::array<std::byte, reserved_size> reserved{};
};

static constexpr size_t metrics_chunk_channel_entry_size = 94U;
static_assert(
  sizeof(MetricsChunkChannelEntry) ==
  metrics_chunk_channel_entry_size); // Metrics chunk channel entry size must never change

/// Metadata chunk trailer
struct __attribute__((packed)) MetadataChunkTrailer
{
  /// Chunk offset to the start of the channel entries section
  uint32_t channel_entries_offset{};

  /// Reserved bytes (set to zero)
  static constexpr auto reserved_size = 4U;
  std::array<std::byte, 4U> reserved{};

  /// Common chunk trailer
  ChunkTrailerCommon common_trailer{};
};

static constexpr size_t metadata_chunk_trailer_size = 20U;
static_assert(
  sizeof(MetadataChunkTrailer) == metadata_chunk_trailer_size); // Metadata chunk trailer size must never change

/// Metadata chunk channel entry
struct __attribute__((packed)) MetadataChunkChannelEntry
{
  /// Channel ID
  uint16_t channel_id{};

  /// Channel compression type
  CompressionType compression_type{CompressionType::none};

  /// Message encoding
  MessageEncoding message_encoding{MessageEncoding::undefined};

  /// Schema encoding, should be undefined if the message encoding is undefined
  SchemaEncoding schema_encoding{SchemaEncoding::undefined};

  /// Channel name chunk offset
  uint32_t channel_name_offset{};

  /// Channel name size in bytes
  uint32_t channel_name_size{};

  /// Schema name chunk offset
  uint32_t schema_name_offset{};

  /// Schema name size in bytes
  uint32_t schema_name_size{};

  /// Schema definition chunk offset
  uint32_t schema_definition_offset{};

  /// Schema definition size in bytes
  uint32_t schema_definition_size{};

  /// Channel type
  ChannelType channel_type{};

  /// Reserved bytes (set to zero)
  static constexpr auto reserved_size = 3U;
  std::array<std::byte, 3U> reserved{};
};

static constexpr size_t metadata_chunk_channel_entry_size = 36U;
static_assert(
  sizeof(MetadataChunkChannelEntry) ==
  metadata_chunk_channel_entry_size); // Metadata chunk channel entry size must never change

} // namespace clockwork_logging::offboard
