// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/channel_type_clk_cc.hh"
#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/decompress_option.hh"
#include "clockwork/logging/lite_compressor.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/message_encoding_clk_cc.hh"
#include "clockwork/logging/onboard/log_format.hh"
#include "clockwork/logging/onboard/types.hh"
#include "clockwork/logging/schema_encoding_clk_cc.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"

#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace clockwork_logging::onboard
{

/// Onboard log reader
///
/// @tparam BufferedReaderType Buffered reader type
template <typename BufferedReaderType>
class Reader
{
public:
  /// Constructor
  /// @param[in] memory_resource Memory resource
  /// @param[in] log_path Log directory path
  /// @param[in] buffered_reader Buffered reader
  /// @param[in] metadata_map_option Metadata map option
  Reader(
    jewels::memory::MemoryResource memory_resource,
    std::string_view log_path,
    std::shared_ptr<BufferedReaderType> buffered_reader,
    MetadataMapOption metadata_map_option = MetadataMapOption::disable)
    requires DiskBufferedReaderType<BufferedReaderType>;

  /// Constructor
  /// @param[in] memory_resource Memory resource
  /// @param[in] log_files Log files to be read
  /// @param[in] buffered_reader Buffered reader
  /// @param[in] metadata_map_option Metadata map option
  Reader(
    jewels::memory::MemoryResource memory_resource,
    const std::pmr::list<std::pmr::string>& log_files,
    std::shared_ptr<BufferedReaderType> buffered_reader,
    MetadataMapOption metadata_map_option = MetadataMapOption::disable)
    requires DiskBufferedReaderType<BufferedReaderType>;

  /// Constructor
  /// @param[in] memory_resource Memory resource
  /// @param[in] log_data_buffer Memory buffer containing the log to be read
  Reader(jewels::memory::MemoryResource memory_resource, std::span<const std::byte> log_data_buffer)
    requires MemoryBufferedReaderType<BufferedReaderType>;

  ~Reader() noexcept = default;

  Reader(const Reader&) = delete;
  Reader& operator=(const Reader&) = delete;
  Reader(Reader&&) noexcept = default;
  Reader& operator=(Reader&&) noexcept = default;

  /// List the log files under a log directory that contains data for a log timestamp slice
  /// @param[in] memory_resource Memory resource
  /// @param[in] log_path Log directory path
  /// @param[in] log_interval Log timestamp interval to be read
  /// @return List of log files sorted by file nanme or LogError on failure
  [[nodiscard]] static LogExpected<std::pmr::list<std::pmr::string>> list_log_files_for_interval(
    jewels::memory::MemoryResource memory_resource,
    std::string_view log_path,
    LogInterval log_interval,
    const std::shared_ptr<BufferedReaderType>& buffered_reader)
    requires DiskBufferedReaderType<BufferedReaderType>;

  /// Open a log
  /// @param[in] maybe_log_interval Optional transmit time interval to be read
  /// @param[in] time_filter_option Time filter option
  /// @param[in] decompress_option Decompress option
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> open(
    std::optional<LogInterval> maybe_log_interval = std::nullopt,
    TimeFilterOption time_filter_option = TimeFilterOption::log_time,
    DecompressOption decompress_option = DecompressOption::decompress)
    requires DiskBufferedReaderType<BufferedReaderType>;

  /// Open a log
  /// @param[in] maybe_log_interval Optional transmit time interval to be read
  /// @param[in] time_filter_option Time filter option
  /// @param[in] decompress_option Decompress option
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> open(
    std::optional<LogInterval> maybe_log_interval = std::nullopt,
    TimeFilterOption time_filter_option = TimeFilterOption::log_time,
    DecompressOption decompress_option = DecompressOption::decompress)
    requires MemoryBufferedReaderType<BufferedReaderType>;

  /// Close the log if open
  void close();

  /// Test whether the reader is open and has not reached the end of the log
  /// @return True if the reader is open and not at end of log
  [[nodiscard]] explicit operator bool() const noexcept;

  /// Read the next message from the log
  ///
  /// Any data that spans read buffers will be copied into a temporary buffer.
  /// The message will remain valid until the next read.
  ///
  /// @return Next log message or LogError on failure
  [[nodiscard]] LogExpected<LoggedMessage> read_next();

  /// Read the next message from the log into spans backed by the read buffers
  ///
  /// The message will remain valid until the next read.
  ///
  /// @return Next log message or LogError on failure
  [[nodiscard]] LogExpected<ZeroCopyLoggedMessage> zero_copy_read_next();

  /// Get the logged metadata for a channel
  /// @param[in] channel_name
  /// @return Logged channel metadata or LogError on failure
  [[nodiscard]] LogExpected<LoggedChannelMetadata> get_channel_metadata(std::string_view channel_name);

  /// Get the logged metadata for all channels
  /// @return Logged channel metadata or LogError on failure
  [[nodiscard]] LogExpected<std::pmr::unordered_map<std::string_view, LoggedChannelMetadata>> get_channel_metadata_map()
    requires DiskBufferedReaderType<BufferedReaderType>;

  /// Get the log reader error counters
  [[nodiscard]] const ReaderErrorCounters& get_error_counters() const noexcept;

  /// Get the log time interval covered by a log file
  /// @param[in] memory_resource Memory resource
  /// @param[in] file_name Log file name
  /// @param[in] time_filter_option Time filter option
  /// @return File log time interval, empty_log_file if log file is empty, other LogError on failure
  [[nodiscard]] static LogExpected<LogInterval> get_file_log_interval(
    jewels::memory::MemoryResource memory_resource,
    std::string_view file_name,
    TimeFilterOption time_filter_option,
    const std::shared_ptr<BufferedReaderType>& buffered_reader)
    requires DiskBufferedReaderType<BufferedReaderType>;

  /// Get the log time interval covered by a memory log
  /// @param[in] memory_resource Memory resource
  /// @param[in] log_buffer Buffer containing the log
  /// @param[in] time_filter_option Time filter option
  /// @requires The log must have been closed cleanly
  /// @return Log time interval or other LogError on failure
  [[nodiscard]] static LogExpected<LogInterval>
  get_memory_log_interval(std::span<const std::byte> log_buffer, TimeFilterOption time_filter_option)
    requires MemoryBufferedReaderType<BufferedReaderType>;

  /// @return Reference to the buffered reader
  [[nodiscard]] BufferedReaderType& buffered_reader();

  /// @return Reference to the buffered reader
  [[nodiscard]] const BufferedReaderType& buffered_reader() const;

private:
  /// Find the log files for an interval by checking each file called if bisection encounters an error
  /// @param[in] memory_resource Memory resource
  /// @param[in] log_interval Log timestamp interval
  /// @param[in] first_index First log file index to check
  /// @param[in] log_files Files in the log
  /// @param[in] interval_results Results from getting the log file interval for each log file
  /// @return List of log files that contain messages for the interval
  [[nodiscard]] static std::pmr::list<std::pmr::string> list_log_files_for_interval_no_fail(
    jewels::memory::MemoryResource memory_resource,
    LogInterval log_interval,
    size_t first_index,
    const std::pmr::vector<std::pmr::string>& log_files,
    std::pmr::vector<LogExpected<LogInterval>>& interval_results,
    const std::shared_ptr<BufferedReaderType>& buffered_reader)
    requires DiskBufferedReaderType<BufferedReaderType>;

  /// Get the index of the first log file to read for a log timestamp interval
  /// @param[in] memory_resource Memory resource
  /// @param[in] log_interval Log timestamp interval
  /// @param[in] log_files Files in the log
  /// @param[in] interval_results Results from getting the log file interval for each log file
  /// @return Index for the first log file that has messages for the interval or LogError on failure
  [[nodiscard]] static LogExpected<size_t> locate_first_log_file_for_interval(
    jewels::memory::MemoryResource memory_resource,
    LogInterval log_interval,
    const std::pmr::vector<std::pmr::string>& log_files,
    std::pmr::vector<LogExpected<LogInterval>>& interval_results,
    const std::shared_ptr<BufferedReaderType>& buffered_reader)
    requires DiskBufferedReaderType<BufferedReaderType>;

  /// Initialize the list of log all log files to be read
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> initialize_all_log_file_list()
    requires DiskBufferedReaderType<BufferedReaderType>;

  /// Initialize the list of log files to be read
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> initialize_log_file_list()
    requires DiskBufferedReaderType<BufferedReaderType>;

  /// Initialize the log metadata maps
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> initialize_log_metadata_maps()
    requires DiskBufferedReaderType<BufferedReaderType>;

  /// Open a log file
  /// @param[in] file_name Log file name
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> open_log_file(std::string_view file_name)
    requires DiskBufferedReaderType<BufferedReaderType>;

  /// Scan for the next message to be read from the log
  ///
  /// The returned message will remain valid until the next read.
  ///
  /// @return Next log message or LogError on failure
  [[nodiscard]] LogExpected<ZeroCopyLoggedMessage> scan_for_next_log_message();

  /// Scan for the next record in the current log file
  void scan_for_next_record();

  /// Advance past the current record
  /// @param[in] record_size
  void advance_past_current_record(size_t record_size);

  /// Read the next message header from the log file
  /// @return Next message header or LogError on failure
  [[nodiscard]] LogExpected<RecordHeader> read_next_record_header();

  /// Process the next record in the log file
  /// @param[in] record_header Record header
  void process_next_record(const RecordHeader& record_header);

  /// Process a schema record
  /// @param[in] record_header Record header
  void process_schema_record(const RecordHeader& record_header);

  /// Process a channel record
  /// @param[in] record_header Record header
  void process_channel_record(const RecordHeader& record_header);

  /// Process to process the next logged message
  /// @param[in] record_header Record header
  /// @return Zero copy logged message or MonoError on failure
  [[nodiscard]] jewels::expected<ZeroCopyLoggedMessage, jewels::MonoError>
  try_process_next_message(const RecordHeader& record_header);

  /// Process a message record
  /// @param[in] record_header Record header
  /// @return Zero copy logged message or MonoError on failure
  [[nodiscard]] jewels::expected<ZeroCopyLoggedMessage, jewels::MonoError>
  try_process_message_record(const RecordHeader& record_header);

  /// Process an end log file record
  /// @param[in] record_header Record header
  void process_end_log_file_record(const RecordHeader& record_header);

  /// Validate an end log file record
  /// @param[in] record_header Record header
  /// @param[in] trailer Record trailer
  [[nodiscard]] static jewels::expected<void, jewels::MonoError>
  try_validate_end_log_record(const EndLogFileRecordHeader& record_header, const RecordTrailer& trailer);

  /// Read the schema records at the front of each log file.
  ///
  /// Stops reading each file when it encounters a message record or reaches the end of file.
  /// @returns LogError on failure
  [[nodiscard]] LogExpected<void> read_metadata_records();

  /// Handle missing schema metadata
  /// @param[in] schema_id Missing schema ID
  void handle_missing_schema_metadata(uint16_t schema_id);

  /// Handle missing channel metadata
  /// @param[in] channel_id Missing channel ID
  void handle_missing_channel_metadata(uint16_t channel_id);

  /// Get the log time interval covered by a log file by reading the log
  /// @param[in] memory_resource Memory resource
  /// @param[in] file_name Log file name
  /// @param[in] time_filter_option Time filter option
  /// @return File log time interval, empty_log if log file is empty, other LogError on failure
  [[nodiscard]] static LogExpected<LogInterval> get_file_log_interval_from_log(
    jewels::memory::MemoryResource memory_resource,
    std::string_view file_name,
    TimeFilterOption time_filter_option,
    const std::shared_ptr<BufferedReaderType>& buffered_reader)
    requires DiskBufferedReaderType<BufferedReaderType>;

  /// Structure used to store persistent messages from the front of the first log file
  struct SavedPersistentMessage
  {
    /// Sequence number, zero if not available
    uint32_t sequence_number{};
    /// Time that the message was received by the logger
    LogTimestamp log_time;
    /// Time that the message transmitted, set to log_time if not available
    LogTimestamp message_time;
    /// Message header, should be empty if the header is not available
    std::pmr::vector<std::byte> header;
    /// Message data
    std::pmr::vector<std::byte> data;
    /// Message data span
    std::span<const std::byte> data_span;
    /// Message type
    LoggedMessageType message_type{};
    /// Message encoding
    MessageEncoding message_encoding{};
    /// Set when the message data is lite-compressed
    bool is_lite_compressed{};
  };

  /// Convert a saved persitent message to a zero copy logged message
  /// @param[in] channel_name Channel name
  /// @param[in] message Saved persistent message
  /// @param[in] message_time Message time to use
  /// @return Zero copy logged message
  [[nodiscard]] static ZeroCopyLoggedMessage to_zero_copy_logged_message(
    std::string_view channel_name, const SavedPersistentMessage& message, LogTimestamp message_time);

  /// Structure used to store the metadata for a logged schema
  struct SchemaMetadata
  {
    /// Schema name, should be empty if message encoding is undefined
    std::string_view schema_name{};
    /// Schema encoding, should be undefined if the message encoding is undefined
    SchemaEncoding schema_encoding{};
    /// Schema definition string, should be empty if schema encoding is undefined
    std::pmr::string schema_definition{};
  };

  /// Structure used to store the metadata for a logged channel
  struct ChannelMetadata
  {
    /// Channel name
    std::string_view channel_name{};
    /// Channel compression type
    CompressionType compression_type{};
    /// Message encoding
    MessageEncoding message_encoding{};
    /// Schema ID, should be zero if message encoding is undefined
    uint16_t schema_id{};
    /// Channel type
    ChannelType channel_type{};
  };

  /// Channel ID map entry
  struct ChannelIdMapEntry
  {
    /// Channel name
    std::pmr::string channel_name{};

    /// Message encoding
    MessageEncoding message_encoding{};
  };

  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;

  /// Optional log path
  std::optional<std::pmr::string> maybe_log_path_;

  /// List of all files to be read
  std::pmr::list<std::pmr::string> all_log_files_;

  /// Metadata map option
  MetadataMapOption metadata_map_option_{MetadataMapOption::disable};

  /// Buffered reader
  std::shared_ptr<BufferedReaderType> buffered_reader_;

  /// Size of the current message record, used to persist the data from the last call to read
  std::optional<size_t> maybe_current_message_record_size_{};

  /// Buffer used to return message headers in a single span
  std::pmr::vector<std::byte> header_buffer_;

  /// Buffer used to return message data in a single span
  std::pmr::vector<std::byte> data_buffer_;

  /// List of log file paths to be read
  std::pmr::list<std::pmr::string> log_files_;

  /// Buffer used to read memory logs
  std::span<const std::byte> log_data_buffer_;

  /// Log buffer span for memory logs
  std::span<const std::byte> log_buffer_span_;

  /// Optional log timestamp interval to be read
  std::optional<LogInterval> maybe_log_interval_;

  /// Time filter option
  TimeFilterOption time_filter_option_{};

  /// Decompress option
  DecompressOption decompress_option_{};

  /// Map from channel ID to channel name
  std::pmr::unordered_map<uint16_t, ChannelIdMapEntry> channel_id_map_;

  /// Map from channel name to channel metadata
  std::pmr::unordered_map<std::string_view, ChannelMetadata> channel_metadata_map_;

  /// Map from schema ID to schema name
  std::pmr::unordered_map<uint16_t, std::pmr::string> schema_id_map_;

  /// Map from schema name to schema metadata
  std::pmr::unordered_map<std::string_view, SchemaMetadata> schema_metadata_map_;

  /// Error counters
  ReaderErrorCounters error_counters_{};

  /// Map from channel name to persistent messages from the start of the first log file
  std::pmr::map<std::string_view, SavedPersistentMessage> saved_persistent_message_map_;

  /// Map from channel name to the last message time seen for a persistent channel
  std::pmr::map<std::string_view, LogTimestamp> last_persistent_message_time_map_;

  /// First logged message read after the persistent messages at the start of the first log file
  std::optional<ZeroCopyLoggedMessage> maybe_first_logged_message_;

  /// Flag set then the list of all log files has been initialized
  bool all_log_file_list_initialized_{false};

  /// Flag set then the list of log files to be read has been initialized
  bool log_file_list_initialized_{false};

  /// Flag set when the log metadata map has been initialized
  bool log_metadata_maps_initialized_{false};

  /// Flag set when the first message has been read
  bool first_message_read_{false};

  /// Decompressor for lite-compressed messages
  LiteCompressor decompressor_;

  /// Data span for returning decompressed message data
  std::span<const std::byte> decompressed_data_span_;
};

} // namespace clockwork_logging::onboard

#include "clockwork/logging/onboard/reader.inl"
