// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/channel_type.hh"
#include "clockwork/logging/decompress_option.hh"
#include "clockwork/logging/lite_compressor.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/offboard/async_work_queue.hh"
#include "clockwork/logging/offboard/chunk_compressor.hh"
#include "clockwork/logging/offboard/chunk_reader.hh"
#include "clockwork/logging/offboard/chunk_reader_writer_factory.hh"
#include "clockwork/logging/offboard/log_metadata_helper_interface.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "clockwork/logging/offboard/message_chunk_reader.hh"
#include "clockwork/logging/offboard/reader_types.hh"
#include "clockwork/logging/offboard/types.hh"
#include "clockwork/logging/offboard/v1/log_metadata.pb.h"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <list>
#include <map>
#include <memory>
#include <memory_resource>
#include <optional>
#include <queue>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace clockwork_logging::offboard
{

/// Offboard log reader
class Reader
{
private:
  /// Message chunk comparator
  struct MessageChunkComparator
  {
    /// Comparison operator to sort chunks by their next read time
    /// @param[in] lhs Left hand side
    /// @param[in] rhs Right hand side
    /// @return True if lhs should be read after rhs
    [[nodiscard]] bool operator()(
      const jewels::memory::NonNullSharedPtr<MessageChunkReader>& lhs,
      const jewels::memory::NonNullSharedPtr<MessageChunkReader>& rhs)
    {
      const auto maybe_lhs_message_id = lhs->get_next_message_identifier();
      const auto maybe_rhs_message_id = rhs->get_next_message_identifier();
      return (maybe_lhs_message_id && !maybe_rhs_message_id) ||
             (*maybe_lhs_message_id == *maybe_rhs_message_id && lhs->get_channel_type() == ChannelType::regular &&
              rhs->get_channel_type() == ChannelType::persistent) ||
             *maybe_lhs_message_id > *maybe_rhs_message_id;
    }
  };

  /// List entry for a message chunk being prefetched from the log file
  struct PrefetchListEntry
  {
    /// Minimum transmit time stored in the chunk
    LogTimestamp min_transmit_time{};

    /// Channel name
    std::string_view channel_name{};

    /// Channel type
    ChannelType channel_type{};

    /// Message chunk reader future
    std::future<LogExpected<jewels::memory::NonNullSharedPtr<MessageChunkReader>>> chunk_reader_future{};
  };

  /// Class to read log messages from a list of message chunk handles sorted by earliest transmit time
  ///
  /// Initially all message chunks start out in the pending list
  ///
  /// As prefetch requests are scheduled for chunk they are moved from the pending list to the
  /// prefetch list
  ///
  /// A priority queue of message chunk readers is used to get the messages from the log sorted
  /// by transmit time.
  ///
  /// Message chunks added to the priority queue when the earliest timestamp in the head of the prefetch
  /// list is earlier than the earliest timestamp in the priority queue.
  class MessageReader
  {
  public:
    /// Maximum number of outstanding prefetch requests
    static constexpr size_t max_prefetch_requests = 16U;

    /// Constructor
    /// @param[in] memory_resource Memory resource
    /// @param[in] chunk_handles Sorted list of message chunk handles
    /// @param[in] persistent_channels Set of persistent channel names
    /// @param[in] decompress_option Option for whether to decompress lite-compresses messages
    MessageReader(
      jewels::memory::MemoryResource memory_resource,
      std::pmr::list<reader::MessageChunkHandle> chunk_handles,
      const std::pmr::unordered_set<std::pmr::string>& persistent_channels,
      DecompressOption decompress_option);

    ~MessageReader() = default;

    MessageReader(const MessageReader&) = delete;
    MessageReader& operator=(const MessageReader&) = delete;
    MessageReader(MessageReader&&) = delete;
    MessageReader& operator=(MessageReader&&) = delete;

    /// Test whether the message reader has reached the end of the log
    /// @return True if the message reader is not at end of log
    [[nodiscard]] explicit operator bool();

    /// Read the next message from the log
    /// @return Next log message or LogError on failure
    [[nodiscard]] LogExpected<LoggedMessage> read_next();

  private:
    /// Advance pending chunks onto the prefetch list
    void advance_prefetch();

    /// Advance the message reader to the next message
    void advance();

    /// Handle lite compressed messages
    [[nodiscard]] LogExpected<LoggedMessage> handle_lite_compression(const LoggedMessage& message);

    /// Memory resource
    jewels::memory::MemoryResource memory_resource_;

    /// List of pending message chunk handles
    std::pmr::list<reader::MessageChunkHandle> pending_chunk_handles_;

    /// List of message chunk handles being prefetched from the log file
    std::pmr::list<PrefetchListEntry> prefetch_list_;

    /// Priority queue of message chunk readers for the active message chunks
    std::priority_queue<
      jewels::memory::NonNullSharedPtr<MessageChunkReader>,
      std::pmr::vector<jewels::memory::NonNullSharedPtr<MessageChunkReader>>,
      MessageChunkComparator>
      active_readers_;

    /// Pointer to the message chunk reader for the last message read
    std::shared_ptr<MessageChunkReader> prev_message_reader_ptr_;

    /// Flag set then the reader has been initialized
    bool is_initialized_{false};

    /// Async work queue used to prefetch message chunks
    jewels::memory::NonNullSharedPtr<AsyncWorkQueue> async_work_queue_ptr_;

    /// Set of persistent channel names
    std::pmr::unordered_set<std::string_view> persistent_channels_;

    /// Set of persistent channels that have read the first persistent message.
    /// Repeated persistent channel messages are only read if they are the first
    /// message seen on the channel.
    std::pmr::unordered_set<std::string_view> read_persistent_channels_;

    /// Decompress option for lite-compressed messages
    DecompressOption decompress_option_;

    /// Lite compressor
    LiteCompressor lite_compressor_;
  };

  /// Log file reader state
  class FileReaderState
  {
  public:
    /// Constructor
    /// @param[in] memory_resource Memory resource
    /// @param[in] chunk_reader_ptr Chunk reader pointer
    /// @param[in] chunk_compressor_ptr Chunk compressor pointer
    FileReaderState(
      jewels::memory::MemoryResource memory_resource,
      jewels::memory::NonNullSharedPtr<ChunkReader> chunk_reader_ptr,
      jewels::memory::NonNullSharedPtr<ChunkCompressor> chunk_compressor_ptr);

    /// Get the message handles needed to read the desired messages from this log file
    /// @param[in] maybe_desired_channels Optional set of desired channels
    /// @param[in] maybe_log_interval Optional log interval
    /// @return List of message chunk handles sorted by earliest transmit time or LogError on failure
    [[nodiscard]] LogExpected<std::pmr::list<reader::MessageChunkHandle>> get_message_chunk_list(
      const std::optional<std::pmr::unordered_set<std::pmr::string>>& maybe_desired_channels = {},
      std::optional<LogInterval> maybe_log_interval = {});

    /// Get the channel metadata
    /// @return Log file metadata or LogError on failure
    [[nodiscard]] LogExpected<
      jewels::memory::ObjectPtr<const std::pmr::unordered_map<uint16_t, reader::LoggedChannelInfo>>>
    get_metadata();

    /// Get the metadata for a specific channel
    /// @param[in] channel_name Channel name
    /// @return Channel metadata or LogError on failure
    [[nodiscard]] LogExpected<jewels::memory::ObjectPtr<const reader::LoggedChannelInfo>>
    get_channel_metadata(std::string_view channel_name);

    /// Get the log file metrics
    /// @return Log file metrics or LogError on failure
    [[nodiscard]] LogExpected<jewels::memory::ObjectPtr<const reader::LogMetrics>> get_metrics();

    /// Get the log file URI
    /// @return Log file URI
    [[nodiscard]] const LogUri& get_file_uri() const;

  private:
    /// Load the metadata from the log file
    /// @return LogError on failure
    [[nodiscard]] LogExpected<void> load_metadata();

    /// Memory resource
    jewels::memory::MemoryResource memory_resource_;

    /// Chunk reader for the log file
    jewels::memory::NonNullSharedPtr<ChunkReader> chunk_reader_ptr_;

    /// Chunk compressor pointer
    jewels::memory::NonNullSharedPtr<ChunkCompressor> chunk_compressor_ptr_;

    /// Optional log file trailer information, valid once the log file trailer has been read
    std::shared_ptr<reader::LogFileTrailerInfo> log_file_trailer_info_ptr_;

    /// Channel metadata map, valid once the metadata has been read
    std::shared_ptr<std::pmr::unordered_map<uint16_t, reader::LoggedChannelInfo>> metadata_map_ptr_;

    /// Map from channel name to channel metadata, valid once the metadata has been read
    std::pmr::unordered_map<std::string_view, jewels::memory::ObjectPtr<const reader::LoggedChannelInfo>>
      channel_metadata_map_;

    /// Log metrics, valid once the metrics have been read
    std::shared_ptr<reader::LogMetrics> log_metrics_ptr_;

    /// Flag set when the file cannot be accessed
    bool s3_access_is_denied_{false};
  };

public:
  /// Number of worker threads in the async thread pool
  static constexpr size_t num_worker_threads = 16U;

  /// Constructor
  /// @param[in] memory_resource Memory resource
  /// @param[in] uri_str Log URI
  Reader(jewels::memory::MemoryResource memory_resource, std::string_view uri_str);

  ~Reader() = default;

  Reader(const Reader&) = delete;
  Reader& operator=(const Reader&) = delete;
  Reader(Reader&&) = delete;
  Reader& operator=(Reader&&) = delete;

  /// Open the reader
  /// @param[in] maybe_desired_channels Optional set of desired channels
  /// @param[in] maybe_log_interval Optional log interval
  /// @param[in] decompress_option Option for whether to decompress lite-compresses messages
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> open(
    const std::optional<std::pmr::unordered_set<std::pmr::string>>& maybe_desired_channels = {},
    std::optional<LogInterval> maybe_log_interval = {},
    DecompressOption decompress_option = DecompressOption::decompress);

  /// Close the log if open
  void close();

  /// Test whether the reader is open and has not reached the end of the log
  /// @return True if the reader is open and not at end of log
  [[nodiscard]] explicit operator bool();

  /// Read the next message from the log
  /// @return Next log message or LogError on failure
  [[nodiscard]] LogExpected<LoggedMessage> read_next();

  /// Get the channel names
  /// @return Logged channel names or LogError on failure
  [[nodiscard]] LogExpected<jewels::memory::ObjectPtr<const std::pmr::unordered_set<std::pmr::string>>> get_channels();

  /// Get the metadata for the channels in the log
  /// @return Channel metadata or LogError on failure
  [[nodiscard]] LogExpected<jewels::memory::ObjectPtr<const std::pmr::map<std::string_view, LoggedChannelMetadata>>>
  get_metadata();

  /// Get the metadata for a single channel
  /// @param[in] channel_name Channel name
  /// @return Channel metadata or LogError on failure
  [[nodiscard]] LogExpected<LoggedChannelMetadata> get_channel_metadata(std::string_view channel_name);

  /// Get the metrics for the channels in the log
  /// @return Channel metrics or LogError on failure
  [[nodiscard]] LogExpected<jewels::memory::ObjectPtr<const LogMetrics>> get_metrics();

  /// Get the time interval
  /// @return Log interval or LogError on failure
  [[nodiscard]] LogExpected<LogInterval> get_log_interval();

private:
  /// Initialize the log metadata helper
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> initialize_log_metadata_helper();

  /// Initialize the log file reader state
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> initialize_file_reader_state();

  /// Get the file reader state
  /// @pre Expects initialize_file_reader_state to have been called
  /// @return File reader state
  FileReaderState& get_file_reader_state_unsafe();

  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;

  /// Log URI string
  std::pmr::string uri_str_;

  /// Chunk reader factory pointer
  ChunkReaderWriterFactory chunk_reader_factory_;

  /// Chunk compressor pointer
  jewels::memory::NonNullSharedPtr<ChunkCompressor> chunk_compressor_ptr_;

  /// Log metadata helper pointer, valid when initialized
  std::shared_ptr<LogMetadataHelperInterface> log_metadata_helper_ptr_;

  /// file reader state pointers, valid when initialized
  std::shared_ptr<std::pmr::vector<FileReaderState>> file_reader_state_ptr_;

  /// Map from log file name to file reader state
  std::pmr::unordered_map<std::pmr::string, jewels::memory::ObjectPtr<FileReaderState>> file_reader_state_map_;

  /// message reader pointer, valid when open
  std::shared_ptr<MessageReader> message_reader_ptr_;

  /// metadata map pointer, valid when initialized
  std::shared_ptr<std::pmr::map<std::string_view, LoggedChannelMetadata>> metadata_map_ptr_;

  /// metrics pointer, valid when initialized
  std::shared_ptr<LogMetrics> metrics_ptr_;

  /// Log metadata protobuf
  ::clockwork::logging::offboard::v1::LogMetadata log_metadata_protobuf_;
};

} // namespace clockwork_logging::offboard
