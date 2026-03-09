// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/channel_type_clk_cc.hh"
#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/lite_compressor.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/offboard/async_work_queue.hh"
#include "clockwork/logging/offboard/channel_message_writer.hh"
#include "clockwork/logging/offboard/chunk_compressor.hh"
#include "clockwork/logging/offboard/chunk_reader_writer_factory.hh"
#include "clockwork/logging/offboard/chunk_writer.hh"
#include "clockwork/logging/offboard/index_chunk_writer.hh"
#include "clockwork/logging/offboard/metadata_chunk_writer.hh"
#include "clockwork/logging/offboard/metrics_chunk_writer.hh"
#include "clockwork/logging/offboard/s3_utils.hh"
#include "clockwork/logging/offboard/types.hh"
#include "clockwork/logging/offboard/v1/log_metadata.pb.h"
#include "clockwork/logging/offboard/writer_config.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/math/constants.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"

#include <wise_enum.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <memory_resource>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace clockwork_logging::offboard
{

/// Writer overwrite mode
WISE_ENUM_CLASS((OverwriteMode, uint8_t), overwrite, dont_overwrite)

/// Offboard log writer
/// @tparam S3UtilsType S3 utility helper class type
template <typename S3UtilsType = S3Utils>
class Writer
{
public:
  /// Number of worker threads in the async thread pool
  static constexpr size_t num_worker_threads = 16U;

  /// Maximum log file size
  static constexpr auto max_file_size = 50U * jewels::math::constants::bytes_per_gib<size_t>;

  /// Header for the log metadata text protobuf file
  static constexpr auto log_metadata_proto_header =
    "# proto-file: clockwork/logging/offboard/v1/log_metadata.proto\n# proto-message: LogMetadata\n";

  /// Constructor
  /// @param[in] memory_resource Memory resource
  explicit Writer(
    jewels::memory::MemoryResource memory_resource,
    MessageChunkIndexFormat message_chunk_index_format = MessageChunkIndexFormat::v2);

  ~Writer() = default;

  Writer(const Writer&) = delete;
  Writer& operator=(const Writer&) = delete;
  Writer(Writer&&) = delete;
  Writer& operator=(Writer&&) = delete;

  /// Open the writer
  /// @param[in] uri_str Log URI
  /// @param[in] config_str Writer config text proto string
  /// @param[in] overwrite_mode Open overwrite mode
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> open(
    std::string_view uri_str,
    std::string_view config_str = {},
    OverwriteMode overwrite_mode = OverwriteMode::dont_overwrite);

  /// Split all of the log files and start writing a new set of files
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> split_log_files();

  /// Close the log
  /// @return Write metrics or LogError on failure
  [[nodiscard]] LogExpected<ChunkWriter::WriteMetrics> close();

  /// Add a channel to the log
  /// @param[in] channel_metadata Logged channel metadata
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> create_channel(const LoggedChannelMetadata& channel_metadata);

  /// Add a clockwork channel to the log, metadata is deduced from logging traits
  /// @tparam T Channel message type
  /// @param[in] channel_name Channel name
  template <clockwork::TappyType T>
  [[nodiscard]] LogExpected<void>
  create_channel(std::string_view channel_name, ChannelType channel_type = ChannelType::regular);

  /// Write a message to the log
  /// @param[in] message Message to log
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> write(const LoggedMessage& message);

  /// Write a message to the log
  /// @param[in] message Message to log
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> write(const ZeroCopyLoggedMessage& message);

  /// Write a tachyon message to the log
  /// @tparam T Tachyon message type
  /// @param[in] channel_name Channel name
  /// @param[in] sequence_number Sequence number
  /// @param[in] log_time Message log time
  /// @param[in] transmit_time Message transmit time
  /// @param[in] message Message to log
  /// @param[in] is_repeated_persistent Flag indicating a repeated persistent channel message from a prior log
  /// @return LogError on failure
  template <clockwork::TappyType T>
  [[nodiscard]] LogExpected<void> write(
    std::string_view channel_name,
    uint32_t sequence_number,
    LogTimestamp log_time,
    LogTimestamp transmit_time,
    const T& message,
    bool is_repeated_persistent = false);

private:
  /// Build the log metadata protobuf
  /// @return Log metadata or LogError on failure
  [[nodiscard]] ::clockwork::logging::offboard::v1::LogMetadata get_log_metadata_protobuf() const;

  /// Open writer state for a log file
  class FileWriterState
  {
  public:
    /// Metadata about a file stored in the log
    struct LogFileMetadata
    {
      /// Log file name
      std::pmr::string log_file_name;

      /// Transmit time interval of the messages written to the file, if any
      std::optional<LogInterval> maybe_transmit_time_interval;
    };

    /// Constructor
    /// @param[in] memory_resource Memory resource
    /// @param[in] message_chunk_index_format Message chunk index format
    /// @param[in] file_uri_prefix Log file URI prefix
    /// @param[in] async_work_queue_ptr Async work queue poijnter
    FileWriterState(
      jewels::memory::MemoryResource memory_resource,
      MessageChunkIndexFormat message_chunk_index_format,
      std::string_view file_uri_prefix,
      jewels::memory::NonNullSharedPtr<AsyncWorkQueue> async_work_queue_ptr);

    ~FileWriterState() = default;

    FileWriterState(const FileWriterState&) = delete;
    FileWriterState& operator=(const FileWriterState&) = delete;
    FileWriterState(FileWriterState&&) = delete;
    FileWriterState& operator=(FileWriterState&&) = delete;

    /// Open the log file
    /// @param[in] chunk_writer_factory Chunk writer factory
    /// @return LogError on failure
    [[nodiscard]] LogExpected<void> open(ChunkReaderWriterFactory<S3UtilsType>& chunk_writer_factory);

    /// Split the log file
    /// @param[in] chunk_writer_factory Chunk writer factory
    /// @return LogError on failure
    [[nodiscard]] LogExpected<void> split_log_file(ChunkReaderWriterFactory<S3UtilsType>& chunk_writer_factory);

    /// Get the current log file size in bytes
    /// @return File size or LogError on failure
    [[nodiscard]] LogExpected<size_t> get_file_size() const;

    /// Close the log file
    /// @return Write metrics or LogError on failure
    [[nodiscard]] LogExpected<ChunkWriter::WriteMetrics> close();

    /// Add a channel to the log file
    /// @param[in] channel_metadata Logged channel metadata
    /// @param[in] compression_type Compression type
    /// @return LogError on failure
    [[nodiscard]] LogExpected<void>
    create_channel(const LoggedChannelMetadata& channel_metadata, CompressionType compression_type);

    /// Write a message to the log file
    /// @param[in] message Message to log
    /// @return LogError on failure
    [[nodiscard]] LogExpected<void> write(const ZeroCopyLoggedMessage& message);

    /// Accessor for the log file metadata list
    /// @return Log file metadata list
    [[nodiscard]] const std::pmr::list<LogFileMetadata>& get_log_file_metadata_list() const;

  private:
    /// Memory resource
    jewels::memory::MemoryResource memory_resource_;

    /// Message chunk index format
    MessageChunkIndexFormat message_chunk_index_format_;

    /// Metadata chunk writer
    MetadataChunkWriter metadata_writer_;

    /// Index chunk writer
    IndexChunkWriter index_writer_;

    /// Metrics chunk writer
    MetricsChunkWriter metrics_writer_;

    /// Map from channel ID to channel message writer
    std::pmr::unordered_map<uint16_t, ChannelMessageWriter> channel_writer_map_;

    /// Log file metadata list
    std::pmr::list<LogFileMetadata> log_file_metadata_list_;

    /// Log file sequence number counter
    size_t file_sequence_number_{0U};

    /// Log file URI prefix
    std::pmr::string file_uri_prefix_;

    /// Chunk writer for the log file
    std::shared_ptr<ChunkWriter> chunk_writer_ptr_;

    /// Chunk compressor
    jewels::memory::NonNullSharedPtr<ChunkCompressor> chunk_compressor_ptr_;

    /// Async work queue pointer
    jewels::memory::NonNullSharedPtr<AsyncWorkQueue> async_work_queue_ptr_;

    /// Accumulated write metrics
    ChunkWriter::WriteMetrics write_metrics_{};
  };

  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;

  /// Lite compressor
  LiteCompressor lite_compressor_;

  /// Message chunk index format
  MessageChunkIndexFormat message_chunk_index_format_;

  /// Async work queue pointer
  jewels::memory::NonNullSharedPtr<AsyncWorkQueue> async_work_queue_ptr_;

  /// Chunk writer factory
  ChunkReaderWriterFactory<S3UtilsType> chunk_writer_factory_;

  /// Log file URI prefix
  std::pmr::string file_uri_prefix_;

  /// Log URI string
  std::pmr::string uri_str_;

  /// Open writer state
  std::optional<std::pmr::list<FileWriterState>> maybe_file_writer_state_;

  /// Map from log file name prefix to file writer state
  std::pmr::unordered_map<std::pmr::string, jewels::memory::ObjectPtr<FileWriterState>> file_name_prefix_to_writer_map_;

  /// Map from channel name to file writer state
  std::pmr::unordered_map<std::string_view, jewels::memory::ObjectPtr<FileWriterState>> channel_name_to_writer_map_;

  /// Set of persistent channels
  std::pmr::unordered_set<std::string_view> persistent_channels_;

  /// Channel name storage
  std::pmr::list<std::pmr::string> channel_names_;

  /// Writer config instance
  WriterConfig writer_config_;
};

} // namespace clockwork_logging::offboard

#include "clockwork/logging/offboard/writer.inl"
