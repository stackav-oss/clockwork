// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/offboard/chunk_reader_writer_factory.hh"
#include "clockwork/logging/offboard/log_metadata_helper_interface.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "jewels/memory/memory_resource.hh"

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace clockwork_logging::offboard
{

/// Interface class implemented by helper classes for log metadata files
class LogMetadataHelperInterface
{
public:
  LogMetadataHelperInterface() noexcept = default;

  virtual ~LogMetadataHelperInterface() noexcept = default;

  LogMetadataHelperInterface(const LogMetadataHelperInterface& other) = delete;
  LogMetadataHelperInterface& operator=(const LogMetadataHelperInterface& other) = delete;
  LogMetadataHelperInterface(LogMetadataHelperInterface&&) noexcept = default;
  LogMetadataHelperInterface& operator=(LogMetadataHelperInterface&&) noexcept = default;

  /// Initialize the metadata file helper
  /// @param[in] metadata_file_uri Log metadata file URI
  /// @param[in] chunk_reader_factory Chunk reader/writer factory
  /// @return LogError on failure
  [[nodiscard]] virtual LogExpected<void>
  initialize(const LogUri& metadata_file_uri, ChunkReaderWriterFactory& chunk_reader_factory) = 0;

  /// Get a list of log files to that are part of the log and contain messages for
  /// the desired channels and desired time range
  /// @param[in] maybe_desired_channels Optional set of desired channels
  /// @param[in] maybe_transmit_time_interval Optional transmit time interval to be read
  /// @return Filtered log files containing messages to be read or LogError on failure
  [[nodiscard]] virtual LogExpected<std::pmr::vector<std::pmr::string>> list_log_files(
    const std::optional<std::pmr::unordered_set<std::pmr::string>>& maybe_desired_channels,
    const std::optional<LogInterval>& maybe_transmit_time_interval) const = 0;

  /// Get the transmit time interval for the log
  /// @return Transmit time interval
  [[nodiscard]] virtual LogInterval get_transmit_time_interval() const = 0;

  /// Get the channels stored in the log (including persistent channels)
  /// @return Logged channels
  [[nodiscard]] virtual const std::pmr::unordered_set<std::pmr::string>& get_channels() const = 0;

  /// Get the persistent channels stored in the log
  /// @return Logged persistent channels
  [[nodiscard]] virtual const std::pmr::unordered_set<std::pmr::string>& get_persistent_channels() const = 0;
};

} // namespace clockwork_logging::offboard
