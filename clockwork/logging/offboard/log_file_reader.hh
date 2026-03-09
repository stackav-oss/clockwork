// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/offboard/chunk_compressor.hh"
#include "clockwork/logging/offboard/chunk_reader.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "clockwork/logging/offboard/reader_types.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"

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

/// Class to read from a single log file
class LogFileReader
{
public:
  /// Constructor
  /// @param[in] memory_resource Memory resource
  /// @param[in] chunk_reader_ptr Chunk reader pointer
  /// @param[in] chunk_compressor_ptr Chunk compressor pointer
  LogFileReader(
    jewels::memory::MemoryResource memory_resource,
    jewels::memory::NonNullSharedPtr<ChunkReader> chunk_reader_ptr,
    jewels::memory::NonNullSharedPtr<ChunkCompressor> chunk_compressor_ptr);

  ~LogFileReader() = default;

  LogFileReader(const LogFileReader&) = delete;
  LogFileReader& operator=(const LogFileReader&) = delete;
  LogFileReader(LogFileReader&&) = default;
  LogFileReader& operator=(LogFileReader&&) = default;

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

} // namespace clockwork_logging::offboard
