// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/offboard/chunk_reader_writer_factory.hh"
#include "clockwork/logging/offboard/log_metadata_helper_interface.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "jewels/memory/memory_resource.hh"

#include <functional>
#include <memory>
#include <memory_resource>
#include <string>
#include <unordered_set>
#include <vector>

namespace clockwork_logging::offboard
{

/// Helper class for the log metadata text protobuf file
///
/// The log metadata file contains the log time range and the time range and channels stored in
/// each log file. This class implements helper methods to access the metadata.
class LogUnionFileHelper : public LogMetadataHelperInterface
{
public:
  /// Constructor
  /// @param[in] memory_resource Memory resource
  explicit LogUnionFileHelper(jewels::memory::MemoryResource memory_resource);

  ~LogUnionFileHelper() noexcept override = default;

  LogUnionFileHelper(const LogUnionFileHelper& other) = delete;
  LogUnionFileHelper& operator=(const LogUnionFileHelper& other) = delete;
  LogUnionFileHelper(LogUnionFileHelper&&) noexcept = default;
  LogUnionFileHelper& operator=(LogUnionFileHelper&&) noexcept = default;

  /// @see LogMetadataHelperInterface::initialize
  [[nodiscard]] LogExpected<void>
  initialize(const LogUri& metadata_file_uri, ChunkReaderWriterFactory& chunk_reader_factory) override;

  /// @see LogMetadataHelperInterface::list_log_files
  [[nodiscard]] LogExpected<std::pmr::vector<std::pmr::string>> list_log_files(
    const std::optional<std::pmr::unordered_set<std::pmr::string>>& maybe_desired_channels,
    const std::optional<LogInterval>& maybe_transmit_time_interval) const override;

  /// Get the transmit time interval for the log
  /// @return Transmit time interval
  [[nodiscard]] LogInterval get_transmit_time_interval() const override;

  /// Get the channels stored in the log (including persistent channels)
  [[nodiscard]] const std::pmr::unordered_set<std::pmr::string>& get_channels() const override;

  /// Get the persistent channels stored in the log
  [[nodiscard]] const std::pmr::unordered_set<std::pmr::string>& get_persistent_channels() const override;

private:
  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;

  /// Vector of pointers to metadata file helpers for the entries in the union
  std::pmr::vector<std::shared_ptr<LogMetadataHelperInterface>> union_entry_ptrs_;

  /// Transmit time interval
  LogInterval transmit_time_interval_;

  /// Set of all channels stored in the log (including persistent channels)
  std::pmr::unordered_set<std::pmr::string> channels_;

  /// Set of all persistent channels stored in the log
  std::pmr::unordered_set<std::pmr::string> persistent_channels_;
};

/// Helper function to make a log union file helper given a log URI
/// @param[in] memory_resource Memory resource
/// @param[in] log_union_uri Log union metadata file URI
/// @param[in] chunk_reader_factory Chunk reader factory
/// @return Pointer to a log union file helper of LogError on failure
[[nodiscard]] LogExpected<std::shared_ptr<LogMetadataHelperInterface>> make_log_union_file_helper(
  jewels::memory::MemoryResource memory_resource,
  const LogUri& log_union_uri,
  ChunkReaderWriterFactory& chunk_reader_factory);

} // namespace clockwork_logging::offboard
