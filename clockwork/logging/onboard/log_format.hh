// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/message_encoding_clk_cc.hh"
#include "clockwork/logging/schema_encoding_clk_cc.hh"
#include "jewels/math/constants.hh"

#include <wise_enum.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ostream>
#include <string_view>

namespace clockwork_logging::onboard
{

/// Log file name suffix
static constexpr auto log_file_suffix = std::string_view{".olog"};

/// Log file magic number
static constexpr std::array log_magic_number = {'#', 'S', 'T', 'A', 'C', 'K', '0', '\n'};
static_assert(sizeof(log_magic_number) == sizeof(uint64_t));

/// Log record magic number
static constexpr std::array record_magic_number = {'_', 'R', 'E', 'C'};
static_assert(sizeof(record_magic_number) == sizeof(uint32_t));

/// Maximum log record size, used to limit memory allocations when reading corrupt log files
static constexpr size_t max_log_record_size = 64U * jewels::math::constants::bytes_per_mib<size_t>;

/// Maximum schema or channel name string length
static constexpr size_t max_name_string_size = std::numeric_limits<uint16_t>::max();

/// Maximum message header size
static constexpr size_t max_message_header_size = std::numeric_limits<uint16_t>::max();

/// Log record type
WISE_ENUM_CLASS(
  // NOLINTNEXTLINE(performance-enum-size) This enum is part of the RecordHeader and the size is fixed.
  (RecordType, uint16_t),
  // Schema record
  (schema, 1U),
  // Channel record
  (channel, 2U),
  // Message record
  (message, 3U),
  // Message record version 2 (padded to 48 bytes) (NOT SUPPORTED)
  (message_v2, 4U),
  // End log file record
  (end_log_file, 5U))

/// Log header
struct __attribute__((packed)) LogHeader
{
  /// Log magic number
  std::array<char, log_magic_number.size()> magic_number{log_magic_number};
};

/// Log header size
static constexpr size_t log_header_size = 8U;
static_assert(log_header_size == sizeof(LogHeader)); /* Log header size must never change */

/// Record header
struct __attribute__((packed)) RecordHeader
{
  /// Record magic number
  std::array<char, record_magic_number.size()> magic_number{record_magic_number};

  /// Record size in bytes
  uint32_t record_size{0U};

  /// Record type
  RecordType record_type{};

  /// Reserved must be zero
  static constexpr auto reserved_size = 2U;
  std::array<std::byte, reserved_size> reserved{};
};

/// Record header size
static constexpr size_t record_header_size = 12U;
static_assert(record_header_size == sizeof(RecordHeader)); // Record header size must never change

/// Schema record header
struct __attribute__((packed)) SchemaRecordHeader
{
  /// Record header
  RecordHeader header{};

  /// Schema ID
  uint16_t schema_id{0U};

  /// Schema encoding
  SchemaEncoding schema_encoding{SchemaEncoding::undefined};

  /// Schema name length
  uint16_t schema_name_length{0U};

  /// Reserved bytes, must be zero
  static constexpr auto reserved_size = 2U;
  std::array<std::byte, reserved_size> reserved{};
};

/// Schema record header size
static constexpr size_t schema_record_header_size = 20U;
static_assert(schema_record_header_size == sizeof(SchemaRecordHeader)); // Schema record header size must never change

/// Channel record flags
struct __attribute__((packed)) ChannelRecordFlags
{
  /// Channel is persistent
  uint8_t is_persistent : 1;

  /// Reserved bits must be zero
  uint8_t reserved : 7;
};

/// Channel record header
struct __attribute__((packed)) ChannelRecordHeader
{
  /// Header
  RecordHeader header{};

  /// Channel ID
  uint16_t channel_id{0U};

  /// Schema ID
  uint16_t schema_id{0U};

  /// Compression type
  CompressionType compression_type{CompressionType::none};

  /// Message encoding
  MessageEncoding message_encoding{MessageEncoding::undefined};

  /// Channel flags
  ChannelRecordFlags flags{};

  /// Reserved bytes, must be zero
  static constexpr auto reserved_size = 1U;
  std::array<std::byte, reserved_size> reserved{};
};

/// Channel record header size
static constexpr size_t channel_record_header_size = 22U;
static_assert(
  channel_record_header_size == sizeof(ChannelRecordHeader)); // Channel record header size must never change

/// Message record flags
struct __attribute__((packed)) MessageRecordFlags
{
  /// Message is persistent
  uint8_t is_persistent : 1;

  /// Message is repeated from a prior log file
  uint8_t is_repeated : 1;

  /// Message is lite-compressed
  uint8_t is_lite_compressed : 1;

  /// Reserved bits must be zero
  uint8_t reserved : 5;
};

/// Message record header
struct __attribute__((packed)) MessageRecordHeader
{
  /// Header
  RecordHeader header{};

  /// Channel ID
  uint16_t channel_id{0U};

  /// Message header length
  uint16_t header_length{0U};

  /// Sequence number, set to zero if not available
  uint32_t sequence_number{0U};

  /// Log time in nanoseconds since the start of the unix epoch
  int64_t log_time_ns{0};

  /// Message time in nanoseconds since the start of the unix epoch, set to log_time if not available
  int64_t message_time_ns{0};

  /// Message flags
  MessageRecordFlags flags{};

  /// Reserved bytes, must be zero
  static constexpr auto reserved_size = 1U;
  std::array<std::byte, reserved_size> reserved{};
};

/// Message record header size
static constexpr size_t message_record_header_size = 38U;
static_assert(
  message_record_header_size == sizeof(MessageRecordHeader)); // Message record header size must never change

/// Message record header version 2 (NOT SUPPORTED)
struct __attribute__((packed)) MessageRecordHeaderV2
{
  /// Header
  RecordHeader header{};

  /// Channel ID
  uint16_t channel_id{0U};

  /// Message header length
  uint16_t header_length{0U};

  /// Sequence number, set to zero if not available
  uint32_t sequence_number{0U};

  /// Log time in nanoseconds since the start of the unix epoch
  int64_t log_time_ns{0};

  /// Message time in nanoseconds since the start of the unix epoch, set to log_time if not available
  int64_t message_time_ns{0};

  /// Number of pad bytes between this record header and the comms message header
  uint8_t pad1_size{0U};

  /// Number of bytes between the comms message header and the message data
  uint8_t pad2_size{0U};

  /// Number of bytes between the message data and the record trailer
  uint8_t pad3_size{0U};

  /// Message flags
  MessageRecordFlags flags{};

  /// Reserved bytes, must be zero
  static constexpr auto reserved_size = 4U;
  std::array<std::byte, reserved_size> reserved{};
};

/// End log file record header
struct __attribute__((packed)) EndLogFileRecordHeader
{
  /// Header
  RecordHeader header{};

  /// Earliest recorded log time
  int64_t min_log_time_ns{0};

  /// Latest recorded log time
  int64_t max_log_time_ns{0};

  /// Earliest recorded mesage time
  int64_t min_message_time_ns{0};

  /// Latest recorded mesage time
  int64_t max_message_time_ns{0};

  /// Flag set if log contains messages
  bool has_messages{false};

  /// Reserved bytes, must be zero
  static constexpr auto reserved_size = 11U;
  std::array<std::byte, reserved_size> reserved{};
};

/// End log file record header size
static constexpr size_t end_log_file_record_header_size = 56U;
static_assert(end_log_file_record_header_size == sizeof(EndLogFileRecordHeader)); // Record header size must never chan
/// Record trailer
struct __attribute__((packed)) RecordTrailer
{
  /// 64 bit XXH3 checksum
  uint64_t xxh3_checksum{};
};

/// Record trailer size
static constexpr size_t record_trailer_size = 8U;
static_assert(record_trailer_size == sizeof(RecordTrailer)); // Record trailer size must never change

/// Output stream insertion operator for record type
/// @param[in] ostream Output stream
/// @param[in] value Enum value
/// @return Output stream reference
inline std::ostream& operator<<(std::ostream& ostream, RecordType value);

/// Output stream insertion operator for schema encoding
/// @param[in] ostream Output stream
/// @param[in] value Enum value
/// @return Output stream reference
inline std::ostream& operator<<(std::ostream& ostream, SchemaEncoding value);

/// Output stream insertion operator for message encoding
/// @param[in] ostream Output stream
/// @param[in] value Enum value
/// @return Output stream reference
inline std::ostream& operator<<(std::ostream& ostream, MessageEncoding value);

/// Output stream insertion operator for compression type
/// @param[in] ostream Output stream
/// @param[in] value Enum value
/// @return Output stream reference
inline std::ostream& operator<<(std::ostream& ostream, CompressionType value);

} // namespace clockwork_logging::onboard

#include "clockwork/logging/onboard/log_format.inl"
