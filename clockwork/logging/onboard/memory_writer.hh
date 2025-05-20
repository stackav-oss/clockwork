// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/channel_type.hh"
#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/message_encoding.hh"
#include "clockwork/logging/onboard/log_format.hh"
#include "clockwork/logging/onboard/types.hh"
#include "clockwork/logging/onboard/writer_state.hh"
#include "clockwork/logging/schema_encoding.hh"
#include "jewels/math/constants.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

namespace clockwork_logging::onboard
{

/// Onboard log memory writer
///
/// Onboard writer that writes logs into a memory buffer.
class MemoryWriter
{
  /// Struct used to store the schema metadata that has been added to the log
  struct SchemaMetadata
  {
    /// Schema identifier
    uint16_t schema_id{0U};

    /// Schema name
    std::pmr::string schema_name{};

    /// Schema encoding
    SchemaEncoding schema_encoding{SchemaEncoding::undefined};

    /// Schema definition
    std::pmr::string schema_definition{};
  };

  /// Struct used to store the channel metadata that has been added to the log
  struct ChannelMetadata
  {
    /// Channel identifier
    uint16_t channel_id{0U};

    /// Schema ID, zero indicates that no schema was specified
    uint16_t schema_id{0U};

    /// Channel name
    std::pmr::string channel_name{};

    /// Compression type
    CompressionType compression_type{CompressionType::none};

    /// Message encoding
    MessageEncoding message_encoding{MessageEncoding::undefined};

    /// Channel type
    ChannelType channel_type{ChannelType::regular};
  };

public:
  /// Maximum schema definition string length
  static constexpr auto max_schema_definition_string_size = 256U * jewels::math::constants::bytes_per_kib<size_t>;

  /// Constructor
  /// @param[in] memory_resource Memory resource used to allocate memory for the log
  explicit MemoryWriter(jewels::memory::MemoryResource memory_resource);

  ~MemoryWriter() noexcept = default;

  MemoryWriter(const MemoryWriter&) = delete;
  MemoryWriter& operator=(const MemoryWriter&) = delete;
  MemoryWriter(MemoryWriter&&) = delete;
  MemoryWriter& operator=(MemoryWriter&&) = delete;

  /// Open the writer
  /// @param[in] log_buffer Buffer used to store the memory log
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> open_log(std::span<std::byte> log_buffer);

  /// Close the log and return the log buffer span
  /// @return Log buffer or LogError on failure
  [[nodiscard]] LogExpected<std::span<const std::byte>> close_log();

  /// Add a channel definition to the log
  /// @param[in] channel_metadata Logged channel metadata
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> add_channel(const LoggedChannelMetadata& channel_metadata);

  /// Add message to the log
  /// @param[in] message Message to log
  /// @param[in] is_lite_compressed True if the message data is lite-compressed
  /// @param[in] is_repeated_persistent True if the message is a repeated persistent message
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void>
  log_message(const Message& message, bool is_lite_compressed, bool is_repeated_persistent);

  /// Add message to the log
  /// @param[in] message Message to log
  /// @param[in] is_lite_compressed True if the message data is lite-compressed
  /// @param[in] is_repeated_persistent True if the message is a repeated persistent message
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void>
  log_message(const ZeroCopyMessage& message, bool is_lite_compressed, bool is_repeated_persistent);

private:
  /// Fill in the message record header for a log message
  /// @param[in] message Message to log
  /// @param[in] is_lite_compressed True if the message is lite-compressed
  /// @param[in] is_repeated_persistent True if the message is a repeated persistent message
  /// @param[out] record_header
  [[nodiscard]] LogExpected<void> fill_message_record_header(
    const ZeroCopyMessage& message,
    bool is_lite_compressed,
    bool is_repeated_persistent,
    MessageRecordHeader& record_header);

  /// Add a channel definition to the log, expects that the metadata has been validated
  /// @param[in] channel_metadata Logged channel metadata
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> add_channel_impl(const LoggedChannelMetadata& channel_metadata);

  /// Add schema metadata to the log if is not already defined
  /// @param[in] schema_name Schema name
  /// @param[in] schema_encoding Schema encoding
  /// @param[in] schema_definition Schema definition
  /// @return Pointer to the schema metadata storage
  [[nodiscard]] jewels::memory::ObjectPtr<const SchemaMetadata>
  add_schema_metadata(std::string_view schema_name, SchemaEncoding schema_encoding, std::string_view schema_definition);

  /// Write schema metadata to the log
  /// @param[in] schema_metadata Schema metadata
  /// @return Log error on failure
  [[nodiscard]] LogExpected<void> write_schema_metadata(const SchemaMetadata& schema_metadata);

  /// Add channel metadata to the log if it is not already defined
  /// @param[in] channel_name Channel name
  /// @param[in] compression_type Channel compression type
  /// @param[in] message_encoding Message encoding
  /// @param[in] channel_type Channel type
  /// @param[in] schema_id Schema ID
  [[nodiscard]] jewels::memory::ObjectPtr<const ChannelMetadata> add_channel_metadata(
    std::string_view channel_name,
    CompressionType compression_type,
    MessageEncoding message_encoding,
    ChannelType channel_type,
    uint16_t schema_id);

  /// Write channel metadata to the log
  /// @param[in] channel_metadata Channel metadata
  /// @return Log error on failure
  [[nodiscard]] LogExpected<void> write_channel_metadata(const ChannelMetadata& channel_metadata);

  /// Write an end log file record to the log
  void write_end_log_file_record();

  /// Copy data into the log buffer
  /// @param[in] data_spans Data spans to be logged
  /// @return LogError on failure
  LogExpected<void> copy_log_data(std::span<std::span<const std::byte>> data_spans);

  /// Copy data into the log buffer
  /// @param[in] data Data to be logged
  /// @return LogError on failure
  LogExpected<void> copy_log_data(std::span<const std::byte> data);

  /// Update the message time range for the log
  /// @param[in] message_time Message transmit time
  /// @param[in] log_time Message log time
  void update_log_time_range(LogTimestamp message_time, LogTimestamp log_time);

  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;

  /// Writer state
  WriterState state_{WriterState::closed};

  /// Amount of space used in the log buffer
  size_t log_buffer_size_{0U};

  /// Log buffer span
  std::span<std::byte> log_buffer_;

  /// Earliest log time written to the log
  std::optional<LogTimestamp> maybe_min_log_timestamp_;

  /// Latest log time written to the log
  std::optional<LogTimestamp> maybe_max_log_timestamp_;

  /// Earliest message time written to the log
  std::optional<LogTimestamp> maybe_min_message_timestamp_;

  /// Latest message time written to the log
  std::optional<LogTimestamp> maybe_max_message_timestamp_;

  /// Storage for the list of schemas that have been added to the log
  std::pmr::list<SchemaMetadata> schema_metadata_list_;

  /// Storage for the list of channels that have been added to the log
  std::pmr::list<ChannelMetadata> channel_metadata_list_;

  /// Map from schema name to schema metadata
  std::pmr::unordered_map<std::string_view, jewels::memory::ObjectPtr<const SchemaMetadata>> schema_map_;

  /// Map from channel name to channel channel metadata
  std::pmr::unordered_map<std::string_view, jewels::memory::ObjectPtr<const ChannelMetadata>> channel_map_;

  /// Number of schemas that have been added to the log
  uint16_t schema_count_{0U};

  /// Number of channels that have been added to the log
  uint16_t channel_count_{0U};
};

} // namespace clockwork_logging::onboard
