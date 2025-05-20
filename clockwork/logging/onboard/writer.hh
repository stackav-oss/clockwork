// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/channel_type.hh"
#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/lite_compressor.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/message_encoding.hh"
#include "clockwork/logging/onboard/clockwork_message_handle.hh"
#include "clockwork/logging/onboard/log_format.hh"
#include "clockwork/logging/onboard/null_message_handle.hh"
#include "clockwork/logging/onboard/types.hh"
#include "clockwork/logging/onboard/writer_state.hh"
#include "clockwork/logging/schema_encoding.hh"
#include "clockwork/logging/writers/rate_filter.hh"
#include "jewels/aligner/aligner.hh"
#include "jewels/math/constants.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/shared_pool/shared_object_pool.hh"
#include "jewels/time/sync_time.hh"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <list>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace clockwork_logging::onboard
{

/// Writer status returned by the Writer::get_status method
struct WriterStatusResult
{
  /// Status string
  std::pmr::string status_string;

  /// Message rate (msgs/sec)
  double msgs_per_sec;

  /// Data rate (bytes/sec)
  double bytes_per_sec;
};

/// Onboard log writer
///
/// This class is designed to run in a multithreaded environment where one thread
/// is writing messages to the log and another thread is reporting the status.
/// Unless otherwise indicated, methods on this class *SHALL ONLY* be called
/// from the thread doing the writing.
///
/// Errors from write and close operations are reported asynchronously.  After an
/// unrecoverable error occurs the writer becomes unusable. The writer state is set
/// to failed and future operations fail with LogError::failed.
///
/// After a recoverable error occurs (typically EIO) the writer state is set to degraded
/// and the writer remains usable. This interface does not provide for retrying failed
/// writes and there are no guarantees that the data was or was not written to the log
/// file.
///
/// @tparam Policy Onboard writer policy
template <typename Policy>
class Writer
{
  /// Struct used to store state shared between the writer thread and the thread
  /// that reports the writer status
  struct GuardedState
  {
    /// Rate filter window size in seconds
    static constexpr size_t rate_filter_window_sec = 1U;

    /// Constructor
    /// @param[in] memory_resource Memory resource
    explicit GuardedState(jewels::memory::MemoryResource memory_resource);

    /// Writer error code
    std::atomic<std::optional<LogError>> maybe_writer_error;

    /// Number of messages dropped because the write backlog was exceeded
    std::atomic<size_t> drop_count{0U};

    /// Oldest timestamp of data stored in the current async write request
    std::atomic<std::optional<jewels::time::SteadyTime>> maybe_oldest_pending_data_timestamp;

    /// Mutex guarding the status string
    mutable std::mutex mutex;

    /// Human readable status string set when the state is failed or degraded
    std::pmr::string status_string;

    /// Rate filter for the message rate (msgs/sec)
    RateFilter message_rate_filter;

    /// Rate filter for the data rate (bytes/sec)
    RateFilter data_rate_filter;
  };

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

    /// Schema ID, zero indicates that no schema was not specified
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

  /// Struct used to store the last message logged to a persistent channel
  struct PersistentChannelMessage
  {
    /// Message record header
    MessageRecordHeader record_header{};

    /// Message header and data
    std::pmr::vector<std::byte> payload;

    /// Message trailer
    RecordTrailer record_trailer{};
  };

public:
  /// Buffer pool type
  using BufferPoolType = Policy::BufferPoolType;

  /// Buffer size
  static constexpr size_t buffer_size = BufferPoolType::buffer_size;

  /// Buffer alignment
  static constexpr size_t alignment = BufferPoolType::buffer_alignment;

  /// Buffer space to reserve for writing channel and schema metadata to the log in MiB
  static constexpr size_t schema_reserve_mib = Policy::schema_reserve_mib;

  /// Buffer space to reserve for writing persistent messages to the log in MiB
  static constexpr size_t persistent_message_reserve_mib = Policy::persistent_message_reserve_mib;

  /// Maximum outstanding async operations
  static constexpr size_t max_async_requests = Policy::max_async_requests;

  /// Maximum write size in bytes
  static constexpr size_t max_write_size = Policy::max_write_size;

  /// Maximum time to hold data in buffers before writing it to the log
  static constexpr std::chrono::nanoseconds flush_interval = Policy::flush_interval;

  /// Maximum acceptable write backlog for accepting new messages into the log
  static constexpr std::chrono::nanoseconds max_write_backlog = Policy::max_write_backlog;

  /// Maximum log file size
  static constexpr auto max_log_file_size = Policy::max_log_file_size;

  /// Maximum schema definition string length
  static constexpr auto max_schema_definition_string_size = 256U * jewels::math::constants::bytes_per_kib<size_t>;

  /// Time to sleep waiting for async operations to drain
  static constexpr auto drain_sleep_time = std::chrono::milliseconds(10);

  /// Async writer type
  using AsyncWriterType = typename Policy::AsyncWriterType;

  /// Async write request type
  using AsyncWriteRequestType = typename Policy::AsyncWriteRequestType;

  /// Async write request handle type
  using AsyncWriteRequestHandleType = typename Policy::AsyncWriteRequestHandleType;

  /// Message handle type
  using MessageHandleType = typename Policy::MessageHandleType;

  /// Aligner type
  using AlignerType = jewels::Aligner<alignment>;

  /// Constructor
  /// @param[in] init_memory_resource Memory resource used to allocate memory during initialization
  /// @param[in] runtime_memory_resource Memory resource used to allocate memory after initialization
  /// @param[in] max_write_mib_per_sec Maximum write rate in MiB per second
  /// @param[in] writer_environment Writer environment type
  Writer(
    jewels::memory::MemoryResource init_memory_resource,
    jewels::memory::MemoryResource runtime_memory_resource,
    size_t max_write_mib_per_sec,
    std::chrono::nanoseconds max_log_file_duration,
    WriterEnvironment writer_environment);

  /// Constructor
  /// @param[in] init_memory_resource Memory resource used to allocate memory during initialization
  /// @param[in] runtime_memory_resource Memory resource used to allocate memory after initialization
  /// @param[in] buffer_pool_ptr Buffer pool pointer
  /// @param[in] writer_environment Writer environment type
  Writer(
    jewels::memory::MemoryResource init_memory_resource,
    jewels::memory::MemoryResource runtime_memory_resource,
    jewels::memory::NonNullSharedPtr<BufferPoolType> buffer_pool_ptr,
    std::chrono::nanoseconds max_log_file_duration,
    WriterEnvironment writer_environment);

  /// Destructor waits for all outstanding async requests to complete but any buffered data
  /// is not written to the log.  Data maybe be lost if an error occurs while draining the outstanding
  /// async requests. The caller should call close_log followed by drain_async_operations to get notified
  /// of any I/O errors.
  ~Writer() noexcept;

  Writer(const Writer&) = delete;
  Writer& operator=(const Writer&) = delete;
  Writer(Writer&&) = delete;
  Writer& operator=(Writer&&) = delete;

  /// Open the writer
  /// @param[in] log_path Path to the directory that contains the log
  /// @param[in] log_file_prefix Log file name prefix
  /// @param[in] current_steady_time Current steady time
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void>
  open_log(std::string_view log_path, std::string_view log_file_prefix, jewels::time::SteadyTime current_steady_time);

  /// Open the writer in a paused state, logging won't start until resume_logging is called
  /// @param[in] log_path Path to the directory that contains the log
  /// @param[in] log_file_prefix Log file name prefix
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> open_log_paused(std::string_view log_path, std::string_view log_file_prefix);

  /// Pause logging and close the current log file
  /// @param[in] current_steady_time Current steady time
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> pause_logging(jewels::time::SteadyTime current_steady_time);

  /// Resume logging into a new log file
  /// @param[in] current_steady_time Current steady time
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> resume_logging(jewels::time::SteadyTime current_steady_time);

  /// Close the current file and stop writing the the current directory
  /// @param[in] current_steady_time Current steady time
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> close_log(jewels::time::SteadyTime current_steady_time);

  /// Add a channel definition to the log
  /// @param[in] channel_metadata Logged channel metadata
  /// @param[in] current_steady_time Current steady time
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void>
  add_channel(const LoggedChannelMetadata& channel_metadata, jewels::time::SteadyTime current_steady_time);

  /// Add a message to the log
  /// @param[in] message Message to log
  /// @param[in] message_handle Message handle to ensure the data is valid until it has been written
  /// @param[in] current_steady_time Current steady time
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> log_message(
    const Message& message, const MessageHandleType& message_handle, jewels::time::SteadyTime current_steady_time)
    requires(!std::is_same_v<MessageHandleType, ClockworkMessageHandle>);

  /// Add a message to the log blocking as needed to avoid overrunning the log device
  /// @param[in] message Message to log
  /// @param[in] message_handle Message handle to ensure the data is valid until it has been written
  /// @param[in] current_steady_time Current steady time
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> log_message_wait(
    const Message& message, const MessageHandleType& message_handle, jewels::time::SteadyTime current_steady_time)
    requires(!std::is_same_v<MessageHandleType, ClockworkMessageHandle>);

  /// Save a persistent message to the log
  /// @param[in] message Message to log
  /// @param[in] message_handle Message handle to ensure the data is valid until it has been written
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void>
  save_persistent_message(const Message& message, const MessageHandleType& message_handle);

  /// Write a clockwork message to the log from a pinion buffer
  /// @param[in] channel_name Channel name
  /// @param[in] message_handle Clockwork message handle
  /// @param[in] log_time Message log timestamp
  /// @param[in] current_steady_time Current steady time
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> log_clockwork_message(
    std::string_view channel_name,
    const ClockworkMessageHandle& message_handle,
    LogTimestamp log_time,
    jewels::time::SteadyTime current_steady_time);

  /// Write a clockwork message to the log from a pinion buffer blocking as needed to avoid
  /// overrunning the log device
  /// @param[in] channel_name Channel name
  /// @param[in] message_handle Clockwork message handle
  /// @param[in] log_time Message log timestamp
  /// @param[in] current_steady_time Current steady time
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> log_clockwork_message_wait(
    std::string_view channel_name,
    const ClockworkMessageHandle& message_handle,
    LogTimestamp log_time,
    jewels::time::SteadyTime current_steady_time);

  /// Save a persistent clockwork message to the log from a pinion buffer
  /// @param[in] channel_name Channel name
  /// @param[in] message_handle Clockwork message handle
  /// @param[in] log_time Message log timestamp
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> save_persistent_clockwork_message(
    std::string_view channel_name, const ClockworkMessageHandle& message_handle, LogTimestamp log_time)
    requires std::is_same_v<MessageHandleType, NullMessageHandle>;

  /// Add message to the log
  /// @param[in] message Message to log
  /// @param[in] is_lite_compressed True if the message data is lite-compressed
  /// @param[in] current_steady_time Current steady time
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void>
  log_message(const Message& message, bool is_lite_compressed, jewels::time::SteadyTime current_steady_time);

  /// Add message to the log
  /// @param[in] message Message to log
  /// @param[in] is_lite_compressed True if the message data is lite-compressed
  /// @param[in] current_steady_time Current steady time
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void>
  log_message(const ZeroCopyMessage& message, bool is_lite_compressed, jewels::time::SteadyTime current_steady_time);

  /// Add a message to the log blocking as needed to avoid overrunning the log device
  /// @param[in] message Message to log
  /// @param[in] is_lite_compressed True if the message data is lite-compressed
  /// @param[in] current_steady_time Current steady time
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void>
  log_message_wait(const Message& message, bool is_lite_compressed, jewels::time::SteadyTime current_steady_time);

  /// Add a message to the log blocking as needed to avoid overrunning the log device
  /// @param[in] message Message to log
  /// @param[in] is_lite_compressed True if the message data is lite-compressed
  /// @param[in] current_steady_time Current steady time
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> log_message_wait(
    const ZeroCopyMessage& message, bool is_lite_compressed, jewels::time::SteadyTime current_steady_time);

  /// Save a message to the log
  /// @param[in] message Message to save
  /// @param[in] is_lite_compressed True if the message data is lite-compressed
  [[nodiscard]] LogExpected<void> save_persistent_message(const Message& message, bool is_lite_compressed);

  /// Save a message to the log
  /// @param[in] message Message to save
  /// @param[in] is_lite_compressed True if the message data is lite-compressed
  [[nodiscard]] LogExpected<void> save_persistent_message(const ZeroCopyMessage& message, bool is_lite_compressed);

  /// Periodic callback to manage async operations
  /// @param current_steady_time Current steady time
  void periodic_callback(jewels::time::SteadyTime current_steady_time);

  /// Wait for all outstanding async operations to complete
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> drain_async_operations();

  /// Get the current write backlog
  /// @note This method *MAY* be called by the thread that reports the writer state
  /// @param current_steady_time Current steady time
  /// @return Age of the oldest pending write data or log error on failure
  [[nodiscard]] LogExpected<std::chrono::nanoseconds>
  get_write_backlog(jewels::time::SteadyTime current_steady_time) const;

  /// Get the current writer state
  /// @note This method *MAY* be called by the thread that reports the writer state
  /// @return Writer state
  [[nodiscard]] WriterState get_state() const;

  /// Get the writer status from the guarded state
  /// @note This method *MAY* be called by the thread that reports the writer state
  /// @return Status result
  [[nodiscard]] WriterStatusResult get_status();

  /// Get the current log file offset
  /// @return Log file offset, or zero if the writer has failed
  [[nodiscard]] LogExpected<size_t> get_log_file_offset() const;

  /// Accessor for the async writer, used in unit testing
  /// @return Async writer reference
  [[nodiscard]] AsyncWriterType& get_async_writer();

  /// Get and reset the number of messages dropped due to the write backlog being exceeded
  /// @note This method *MAY* be called by the thread that reports the writer state
  /// @post The drop count is reset to zero
  /// @return Drop count
  [[nodiscard]] size_t get_and_reset_drop_count();

  /// Calculate the number of schema reserve buffers
  /// @return Number of buffers reserved for writing schema metadata
  [[nodiscard]] static constexpr size_t calculate_schema_reserve_buffers() noexcept;

  /// Calculate the number of schema reserve async write requests
  /// @return Number of async write requests reserved for writing schema metadata
  [[nodiscard]] static constexpr size_t calculate_schema_reserve_async_write_requests() noexcept;

  /// Calculate the size of the write buffer pool from the maximum write rate and the maximum write backlog
  /// @param[in] max_write_mib_per_sec Maximum write rate in MiB per second
  /// @param[in] max_backlog Maximum write backlog interval
  [[nodiscard]] static size_t
  calculate_write_buffer_pool_size(size_t max_write_mib_per_sec, std::chrono::nanoseconds max_backlog) noexcept;

  /// Stop writing to the current log file and start writing to the next
  /// @param[in] current_steady_time Current steady time
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> split_log(jewels::time::SteadyTime current_steady_time);

private:
  /// Check that the resources needed to write a message are available and the max write backlog has not been exceeded
  /// @param[in] channel_name Channel name
  /// @param[in] current_steady_time
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void>
  pre_write_check(std::string_view channel_name, jewels::time::SteadyTime current_steady_time);

  /// Wait for the async writer to catch up before writing another message to the log
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> pre_write_wait();

  /// Fill in the message record header for a log message
  /// @param[in] message Message to log
  /// @param[in] is_lite_compressed True if the message is lite-compressed
  /// @param[out] record_header
  [[nodiscard]] LogExpected<void> fill_message_record_header(
    const ZeroCopyMessage& message, bool is_lite_compressed, MessageRecordHeader& record_header)
    requires(!std::is_same_v<MessageHandleType, ClockworkMessageHandle>);

  /// Add a message to the log after error checking has been done
  /// @param[in] message Message to log
  /// @param[in] message_handle Message handle to ensure the data is valid until it has been written
  /// @param[in] current_steady_time Current steady time
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> log_message_impl(
    const Message& message, const MessageHandleType& message_handle, jewels::time::SteadyTime current_steady_time)
    requires(!std::is_same_v<MessageHandleType, ClockworkMessageHandle>);

  /// Write a message to the log using zero copy
  /// @param[in] record_header Message record header
  /// @param[in] header Message header
  /// @param[in] data Message data
  /// @param[in] record_trailer Message record trailer
  /// @param[in] message_handle Message handle
  /// @param[in] current_steady_time Current steady time
  [[nodiscard]] LogExpected<void> zero_copy_log_message(
    const MessageRecordHeader& record_header,
    std::span<const std::byte> header,
    std::span<const std::byte> data,
    const RecordTrailer& record_trailer,
    const MessageHandleType& message_handle,
    jewels::time::SteadyTime current_steady_time)
    requires(!std::is_same_v<MessageHandleType, ClockworkMessageHandle>);

  /// Add a clockwork message to the log without zero copy after error checking has been done
  /// @param[in] channel_name Channel name
  /// @param[in] message_handle Clockwork message handle
  /// @param[in] log_time Message log timestamp
  /// @param[in] current_steady_time Current steady time
  [[nodiscard]] LogExpected<void> log_clockwork_message_impl(
    std::string_view channel_name,
    const ClockworkMessageHandle& message_handle,
    LogTimestamp log_time,
    jewels::time::SteadyTime current_steady_time)
    requires std::is_same_v<MessageHandleType, NullMessageHandle>;

  /// Add a message to the log after error checking has been done
  /// @param[in] message Message to log
  /// @param[in] is_lite_compressed True if the message data is lite-compressed
  /// @param[in] current_steady_time Current steady time
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> log_message_impl(
    const ZeroCopyMessage& message, bool is_lite_compressed, jewels::time::SteadyTime current_steady_time);

  /// Write the most recent message from each persistent channel to the log
  /// @param[in] current_steady_time Current steady time
  /// @return Log error on failure
  [[nodiscard]] LogExpected<void>
  write_latest_persistent_channel_messages(jewels::time::SteadyTime current_steady_time);

  /// Write all of the channel and schema metadata to the log
  /// @param[in] current_steady_time Current steady time
  /// @return Log error on failure
  [[nodiscard]] LogExpected<void> write_all_metadata(jewels::time::SteadyTime current_steady_time);

  /// Add a channel definition to the log, expects that the metadata has been validated
  /// @param[in] channel_metadata Logged channel metadata
  /// @param[in] current_steady_time Current steady time
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void>
  add_channel_impl(const LoggedChannelMetadata& channel_metadata, jewels::time::SteadyTime current_steady_time);

  /// Add schema metadata to the log if is not already defined
  /// @param[in] schema_name Schema name
  /// @param[in] schema_encoding Schema encoding
  /// @param[in] schema_definition Schema definition
  /// @return Pointer to the schema metadata storage
  [[nodiscard]] jewels::memory::ObjectPtr<const SchemaMetadata>
  add_schema_metadata(std::string_view schema_name, SchemaEncoding schema_encoding, std::string_view schema_definition);

  /// Write schema metadata to the log
  /// @param[in] schema_metadata Schema metadata
  /// @param[in] current_steady_time Current steady time
  /// @return Log error on failure
  [[nodiscard]] LogExpected<void>
  write_schema_metadata(const SchemaMetadata& schema_metadata, jewels::time::SteadyTime current_steady_time);

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
  /// @param[in] current_steady_time Current steady time
  /// @return Log error on failure
  [[nodiscard]] LogExpected<void>
  write_channel_metadata(const ChannelMetadata& channel_metadata, jewels::time::SteadyTime current_steady_time);

  /// Write an end log file record to the log
  /// @param[in] current_steady_time Current steady time
  /// @return Log error on failure
  [[nodiscard]] LogExpected<void> write_end_log_file_record(jewels::time::SteadyTime current_steady_time);

  /// Asynchronously write data with data copy
  /// @param[in] data_spans Data spans to be logged
  /// @param[in] current_steady_time Current steady time
  /// @return Log error on failure
  [[nodiscard]] LogExpected<void>
  copy_log_data(std::span<std::span<const std::byte>> data_spans, jewels::time::SteadyTime current_steady_time);

  /// Asynchronously write data with data copy
  /// @param[in] data Data to be logged
  /// @param[in] current_steady_time Current steady time
  /// @return Log error on failure
  [[nodiscard]] LogExpected<void>
  copy_log_data(std::span<const std::byte> data, jewels::time::SteadyTime current_steady_time);

  /// Asynchronously write data with zero copy
  /// @param[in] data Data to be logged
  /// @param[in] message_handle Message handle
  /// @param[in] current_steady_time Current steady time
  /// @return Log error on failure
  [[nodiscard]] LogExpected<void> zero_copy_log_data(
    std::span<const std::byte> data,
    const MessageHandleType& message_handle,
    jewels::time::SteadyTime current_steady_time);

  /// Advance the writer to the next buffer
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> advance_to_next_buffer();

  /// Update the message time range for the current file and split the log if needed
  /// @param[in] message_time Message transmit time
  /// @param[in] log_time Message log time
  /// @param[in] current_steady_time Current steady time
  [[nodiscard]] LogExpected<void>
  split_log_if_needed(LogTimestamp message_time, LogTimestamp log_time, jewels::time::SteadyTime current_steady_time);

  /// Set the writer error and status string after a failure
  /// @param[in] writer_error Writer error
  /// @param[in] status_string Human readable status string
  /// @return Returns the writer that was set
  [[nodiscard]] LogError set_writer_error(LogError writer_error, std::string_view status_string);

  /// Update the logging rates when writing a record to the log
  /// @param[in] record_size Message record_size
  /// @param[in] message_count Number of messages logged (set to zero when writing a metadata record)
  void update_logging_rates(size_t record_size, size_t message_count);

  /// Save a persistent message for logging at the start of the next log file
  /// @param[in] record_header Message record header
  /// @param[in] header Message header
  /// @param[in] data_spans Message data spans
  /// @return Copy of the persistent channel message
  [[nodiscard]] PersistentChannelMessage copy_persistent_channel_message(
    const MessageRecordHeader& record_header,
    std::span<const std::byte> header,
    std::span<const std::span<const std::byte>> data_spans);

  /// Save a persistent message for logging at the start of the next log file
  /// @param[in] record_header Message record header
  /// @param[in] header Message header
  /// @param[in] data Message data
  /// @return Copy of the persistent channel message
  [[nodiscard]] PersistentChannelMessage copy_persistent_channel_message(
    const MessageRecordHeader& record_header, std::span<const std::byte> header, std::span<const std::byte> data);

  /// Guarded state shared between the thread that does the writing and the thread
  /// that reports the writer state
  jewels::memory::pmr_unique_ptr<GuardedState> guarded_state_{};

  /// Async write request pool
  jewels::SharedObjectPool<AsyncWriteRequestType> async_write_request_pool_;

  /// Write buffer pool
  jewels::memory::NonNullSharedPtr<BufferPoolType> write_buffer_pool_ptr_;

  /// Async writer
  AsyncWriterType async_writer_;

  /// Runtime memory resource
  jewels::memory::MemoryResource runtime_memory_resource_;

  /// Async write request handle
  AsyncWriteRequestHandleType async_request_handle_;

  /// Max duration of a log file before it gets split
  std::chrono::nanoseconds max_log_file_duration_;

  /// Earliest log time written to the current log file
  std::optional<LogTimestamp> maybe_min_log_timestamp_;

  /// Latest log time written to the current log file
  std::optional<LogTimestamp> maybe_max_log_timestamp_;

  /// Earliest message time written to the current log file
  std::optional<LogTimestamp> maybe_min_message_timestamp_;

  /// Latest message time written to the current log file
  std::optional<LogTimestamp> maybe_max_message_timestamp_;

  /// Storage for the list of schemas that have been added to the log
  std::pmr::list<SchemaMetadata> schema_metadata_list_;

  /// Storage for the list of channels that have been added to the log
  std::pmr::list<ChannelMetadata> channel_metadata_list_;

  /// Map from schema name to schema metadata
  std::pmr::unordered_map<std::string_view, jewels::memory::ObjectPtr<const SchemaMetadata>> schema_map_;

  /// Map from channel name to channel channel metadata
  std::pmr::unordered_map<std::string_view, jewels::memory::ObjectPtr<const ChannelMetadata>> channel_map_;

  /// Map from channel name to the latest persistent message logged on the channel
  std::pmr::unordered_map<std::string_view, PersistentChannelMessage> persistent_channel_message_map_;

  /// Compressor for lite-compressed messages
  LiteCompressor compressor_;

  /// Number of schemas that have been added to the log
  uint16_t schema_count_{0U};

  /// Number of channels that have been added to the log
  uint16_t channel_count_{0U};

  /// Writer environment type
  WriterEnvironment writer_environment_;
};

} // namespace clockwork_logging::onboard

#include "clockwork/logging/onboard/writer.inl"
