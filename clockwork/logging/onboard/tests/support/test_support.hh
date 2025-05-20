// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/onboard/log_format.hh"
#include "clockwork/logging/onboard/types.hh"
#include "jewels/std/expected.hh"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace clockwork_logging::onboard::tests
{

/// Dump the contents of a data span
/// @param[in] label Output line label
/// @param[in] data Data span
void dump_data_span(std::string_view label, std::span<const std::byte> data);

/// Read a file into a vector of bytes
/// @param[in] file_path File path
/// @return Vector of bytes on success
[[nodiscard]] jewels::expected<std::vector<char>, jewels::MonoError> try_read_file(std::string_view file_path);

/// Validate the log header in the bytes read from a file
/// @param[in] file_data Data read from the file
/// @return Offset to the first byte past the header on success
[[nodiscard]] jewels::expected<size_t, jewels::MonoError> try_validate_log_header(std::span<const char> file_data);

/// Validate pad bytes in the log file
/// @param[in] file_data Data read from the file
/// @param[in] offset Pad bytes offset
/// @param[in] length Pad bytes length
/// @return Offset to the first byte past the pad bytes on success
[[nodiscard]] jewels::expected<size_t, jewels::MonoError>
try_validate_pad_bytes(std::span<const char> file_data, size_t offset, size_t length);

/// Validate the common record header
/// @param[in] record_header Record header to validate
/// @param[in] record_size Expected record size
/// @param[in] record_type Exected record type
/// @return MonoError on failure
[[nodiscard]] jewels::expected<void, jewels::MonoError>
try_validate_record_header(const RecordHeader& record_header, size_t record_size, RecordType record_type);

/// Validate the record trailer
/// @param[in] record_data Record data
/// @return MonoError on failure
[[nodiscard]] jewels::expected<void, jewels::MonoError>
try_validate_record_trailer(std::span<const std::byte> record_data);

/// Validate the record trailer
/// @param[in] checksum_spans Record data spans
/// @param[in] trailer_data Record trailer data
/// @return MonoError on failure
[[nodiscard]] jewels::expected<void, jewels::MonoError> try_validate_record_trailer(
  std::span<const std::span<const std::byte>> checksum_spans, std::span<const std::byte> trailer_data);

/// Validate a schema record in a log file
/// @param[in] file_data Data read from the file
/// @param[in] offset Record offset
/// @param[in] channel_metadata Logged channel metadata
/// @param[in] schema_id_map Map from channel name to schema ID
/// @return Offset to the first byte past the record on success
[[nodiscard]] jewels::expected<size_t, jewels::MonoError> try_validate_schema_record(
  std::span<const char> file_data,
  size_t offset,
  const LoggedChannelMetadata& channel_metadata,
  const std::unordered_map<std::string_view, uint16_t>& schema_id_map);

/// Validate a channel record in a log file
/// @param[in] file_data Data read from the file
/// @param[in] offset Record offset
/// @param[in] channel_metadata Logged channel metadata
/// @param[in] schema_id_map Map from channel name to schema ID
/// @param[in] channel_id_map Map from channel name to channel ID
/// @return Offset to the first byte past the record on success
[[nodiscard]] jewels::expected<size_t, jewels::MonoError> try_validate_channel_record(
  std::span<const char> file_data,
  size_t offset,
  const LoggedChannelMetadata& channel_metadata,
  const std::unordered_map<std::string_view, uint16_t>& schema_id_map,
  const std::unordered_map<std::string_view, uint16_t>& channel_id_map);

/// Validate a message record in a log file
/// @param[in] file_data Data read from the file
/// @param[in] offset Record offset
/// @param[in] msg Logged message
/// @param[in] channel_id_map Map from channel name to channel ID
/// @param[in] is_lite_compressed True if the message data is lite compressed
/// @param[in] is_persistent True if the message is from a persistent channel
/// @param[in] is_repeated True if the message is repeated from a prior log
/// @return Offset to the first byte past the record on success
[[nodiscard]] jewels::expected<size_t, jewels::MonoError> try_validate_message_record(
  std::span<const char> file_data,
  size_t offset,
  const Message& msg,
  const std::unordered_map<std::string_view, uint16_t>& channel_id_map,
  bool is_lite_compressed = false,
  bool is_persistent = false,
  bool is_repeated = false);

/// Validate a message record in a log file
/// @param[in] file_data Data read from the file
/// @param[in] offset Record offset
/// @param[in] msg Zero copy logged message
/// @param[in] channel_id_map Map from channel name to channel ID
/// @param[in] is_lite_compressed True if the message data is lite compressed
/// @param[in] is_persistent True if the message is from a persistent channel
/// @param[in] is_repeated True if the message is repeated from a prior log
/// @return Offset to the first byte past the record on success
[[nodiscard]] jewels::expected<size_t, jewels::MonoError> try_validate_message_record(
  std::span<const char> file_data,
  size_t offset,
  const ZeroCopyMessage& msg,
  const std::unordered_map<std::string_view, uint16_t>& channel_id_map,
  bool is_lite_compressed = false,
  bool is_persistent = false,
  bool is_repeated = false);

/// Validate an end log record in a log file
/// @param[in] file_data Data read from the file
/// @param[in] offset Record offset
/// @param[in] has_messages Expected has messages value
/// @param[in] min_log_time Expected min log time
/// @param[in] max_log_time Expected max log time
/// @param[in] min_message_time Expected min message time
/// @param[in] max_message_time Expected max message time
/// @return Offset to the first byte past the record on success
[[nodiscard]] jewels::expected<size_t, jewels::MonoError> try_validate_end_log_file_record(
  std::span<const char> file_data,
  size_t offset,
  bool has_messages,
  LogTimestamp min_log_time,
  LogTimestamp max_log_time,
  LogTimestamp min_message_time,
  LogTimestamp max_message_time);

/// Fill a buffer with random bytes
/// @param[in] buffer Buffer to be filled
void fill_with_random_bytes(std::span<std::byte> buffer);

/// Corrupt a log file by writing data at the specified offset
/// @param[in] file_path File path
/// @param[in] offset File offset
/// @param[in] data Data to write
/// @return MonoError on failure
[[nodiscard]] jewels::expected<void, jewels::MonoError>
corrupt_log_file(std::string_view file_path, size_t offset, std::string_view data);

} // namespace clockwork_logging::onboard::tests
