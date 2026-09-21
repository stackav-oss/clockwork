// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/offboard/chunk_reader_writer_factory.hh"
#include "clockwork/logging/offboard/log_metadata_helper_interface.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "jewels/callsig/outparam.hh"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

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
  /// @param[in] excluded_channels Channels to exclude from the log
  /// @param[in] chunk_reader_factory Chunk reader/writer factory
  /// @return LogError on failure
  virtual LogOutcome initialize(
    const LogUri& metadata_file_uri,
    const std::pmr::unordered_set<std::string_view>& excluded_channels,
    ChunkReaderWriterFactory<>& chunk_reader_factory) = 0;

  /// Get a map from log file to channels for the log files that are part of the log and contain messages
  /// for the desired channels and desired time range
  /// @param[in] maybe_desired_channels Optional set of desired channels
  /// @param[in] maybe_transmit_time_interval Optional transmit time interval to be read
  /// @param[out] log_file_map Map from log URI to channel names
  /// @return Success or LogError on failure
  [[nodiscard]] virtual LogOutcome get_log_file_map(
    jewels::Out<std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>>> log_file_map,
    const std::optional<std::pmr::unordered_set<std::pmr::string>>& maybe_desired_channels,
    const std::optional<LogInterval>& maybe_transmit_time_interval,
    const std::optional<std::pmr::unordered_set<std::pmr::string>>& maybe_excluded_channels) const = 0;

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
