// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/offboard/chunk_reader_writer_factory.hh"
#include "clockwork/logging/offboard/log_metadata_helper_interface.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/memory/memory_resource.hh"

#include <functional>
#include <memory>
#include <memory_resource>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace clockwork_logging::offboard
{

/// Helper class that recovers log metadata by reading the log files in the log directory
///
/// Normally the log metadata file contains the log time range and the time range and channels
/// stored in each log file. This class recovers the metadata by reading the log files when the
/// metadata file is missing.
class LogMetadataRecoveryHelper : public LogMetadataHelperInterface
{
public:
  /// Constructor
  /// @param[in] memory_resource Memory resource
  explicit LogMetadataRecoveryHelper(jewels::memory::MemoryResource memory_resource);

  ~LogMetadataRecoveryHelper() noexcept override = default;

  LogMetadataRecoveryHelper(const LogMetadataRecoveryHelper& other) = delete;
  LogMetadataRecoveryHelper& operator=(const LogMetadataRecoveryHelper& other) = delete;
  LogMetadataRecoveryHelper(LogMetadataRecoveryHelper&&) noexcept = default;
  LogMetadataRecoveryHelper& operator=(LogMetadataRecoveryHelper&&) noexcept = default;

  /// @see LogMetadataHelperInterface::initialize
  LogOutcome initialize(
    const LogUri& metadata_file_uri,
    const std::pmr::unordered_set<std::string_view>& excluded_channels,
    ChunkReaderWriterFactory<>& chunk_reader_factory) override;

  /// @see LogMetadataHelperInterface::get_log_file_map
  LogOutcome get_log_file_map(
    jewels::Out<std::pmr::unordered_map<std::pmr::string, std::pmr::unordered_set<std::pmr::string>>> log_file_map,
    const std::optional<std::pmr::unordered_set<std::pmr::string>>& maybe_desired_channels,
    const std::optional<LogInterval>& maybe_transmit_time_interval,
    const std::optional<std::pmr::unordered_set<std::pmr::string>>& maybe_excluded_channels) const override;

  /// Get the transmit time interval for the log
  /// @return Transmit time interval
  [[nodiscard]] LogInterval get_transmit_time_interval() const override;

  /// Get the channels stored in the log (including persistent channels)
  [[nodiscard]] const std::pmr::unordered_set<std::pmr::string>& get_channels() const override;

  /// Get the persistent channels stored in the log
  [[nodiscard]] const std::pmr::unordered_set<std::pmr::string>& get_persistent_channels() const override;

private:
  /// Log file metadata map entry
  struct LogFileMetadataMapEntry
  {
    /// Set of channels stored in the file (including persistent channels)
    std::pmr::unordered_set<std::pmr::string> channels;

    /// Set of persistent channels stored in the file
    std::pmr::unordered_set<std::pmr::string> persistent_channels;

    /// Transmit time interval for the messages in the file
    LogInterval transmit_time_interval;
  };

  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;

  /// Transmit time interval
  LogInterval transmit_time_interval_;

  /// Map from log file name to log file metadata
  std::pmr::unordered_map<std::pmr::string, LogFileMetadataMapEntry> log_file_metadata_map_;

  /// Set of all channels stored in the log (including persistent channels)
  std::pmr::unordered_set<std::pmr::string> channels_;

  /// Set of all persistent channels stored in the log
  std::pmr::unordered_set<std::pmr::string> persistent_channels_;

  /// List of log files found under the log directory, valid when initialized
  std::pmr::vector<LogUri> all_log_files_;
};

/// Helper function to make a log metadata file helper given a log URI
/// @param[in] memory_resource Memory resource
/// @param[in] log_uri Log directory URI
/// @param[in] excluded_channels Channels to exclude from the log
/// @param[in] chunk_reader_factory Chunk reader factory
/// @param[out] helper_ptr Log metadata helper pointer
/// @return Success or LogError on failure
LogOutcome make_log_metadata_recovery_helper(
  jewels::memory::MemoryResource memory_resource,
  const LogUri& log_uri,
  const std::pmr::unordered_set<std::string_view>& excluded_channels,
  ChunkReaderWriterFactory<>& chunk_reader_factory,
  jewels::Out<std::shared_ptr<LogMetadataHelperInterface>> helper_ptr);

} // namespace clockwork_logging::offboard
