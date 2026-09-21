// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/offboard/chunk_reader_writer_factory.hh"
#include "clockwork/logging/offboard/log_metadata_helper_interface.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "clockwork/logging/offboard/v1/log_metadata.pb.h"
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

/// Helper class for the log metadata text protobuf file
///
/// The log metadata file contains the log time range and the time range and channels stored in
/// each log file. This class implements helper methods to access the metadata.
class LogMetadataFileHelper : public LogMetadataHelperInterface
{
public:
  /// Constructor
  /// @param[in] memory_resource Memory resource
  explicit LogMetadataFileHelper(jewels::memory::MemoryResource memory_resource);

  ~LogMetadataFileHelper() noexcept override = default;

  LogMetadataFileHelper(const LogMetadataFileHelper& other) = delete;
  LogMetadataFileHelper& operator=(const LogMetadataFileHelper& other) = delete;
  LogMetadataFileHelper(LogMetadataFileHelper&&) noexcept = default;
  LogMetadataFileHelper& operator=(LogMetadataFileHelper&&) noexcept = default;

  /// Initialize the helper from the log metadata protobuf
  /// @param[in] log_metadata_protobuf Log metadata protobuf
  /// @param[in] log_uri Log URI
  /// @param[in] excluded_channels Channels to exclude from the log
  /// @param[in] chunk_reader_factory Chunk reader/writer factory
  /// @return Success or LogError on failure
  LogOutcome initialize_from_protobuf(
    const ::clockwork::logging::offboard::v1::LogMetadata& log_metadata_protobuf,
    const LogUri& log_uri,
    const std::pmr::unordered_set<std::string_view>& excluded_channels,
    ChunkReaderWriterFactory<>& chunk_reader_factory);

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

/// Helper class for the log amendment text protobuf file
///
/// The log metadata file contains the log time range and the time range and channels stored in
/// each log file. This class implements helper methods to access the metadata.
class LogAmendmentFileHelper : public LogMetadataHelperInterface
{
public:
  /// Constructor
  /// @param[in] memory_resource Memory resource
  /// @param[in] is_merge_union True if the amendment is part of a merge union
  explicit LogAmendmentFileHelper(jewels::memory::MemoryResource memory_resource, bool is_merge_union);

  ~LogAmendmentFileHelper() noexcept override = default;

  LogAmendmentFileHelper(const LogAmendmentFileHelper& other) = delete;
  LogAmendmentFileHelper& operator=(const LogAmendmentFileHelper& other) = delete;
  LogAmendmentFileHelper(LogAmendmentFileHelper&&) noexcept = default;
  LogAmendmentFileHelper& operator=(LogAmendmentFileHelper&&) noexcept = default;

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
  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;

  /// Metadata file helper for the amendment (this) log
  std::shared_ptr<LogMetadataFileHelper> amendment_log_ptr_;

  /// Metadata file helper for the amended log
  std::shared_ptr<LogMetadataHelperInterface> amended_log_ptr_;

  /// Transmit time interval
  LogInterval transmit_time_interval_;

  /// Set of all channels stored in the log (including persistent channels)
  std::pmr::unordered_set<std::pmr::string> channels_;

  /// Set of all persistent channels stored in the log
  std::pmr::unordered_set<std::pmr::string> persistent_channels_;

  /// True if the log is part of a merge union
  bool is_merge_union_;
};

/// Helper class for the log union text protobuf file
///
/// The log union metadata file contains an entry for each log in the union.
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
/// @param[in] excluded_channels Channels to exclude from the log
/// @param[in] chunk_reader_factory Chunk reader factory
/// @param[out] helper_ptr Log metadata helper pointer
/// @return Success or LogError on failure
LogOutcome make_log_union_file_helper(
  jewels::memory::MemoryResource memory_resource,
  const LogUri& log_union_uri,
  const std::pmr::unordered_set<std::string_view>& excluded_channels,
  ChunkReaderWriterFactory<>& chunk_reader_factory,
  jewels::Out<std::shared_ptr<LogMetadataHelperInterface>> helper_ptr);

/// Helper function to make a log amendment file helper given a log URI
/// @param[in] memory_resource Memory resource
/// @param[in] log_amendment_uri Log amendment metadata file URI
/// @param[in] excluded_channels Channels to exclude from the kog
/// @param[in] is_merge_union True if the log is part of a merge union
/// @param[in] chunk_reader_factory Chunk reader factory
/// @param[out] helper_ptr Log metadata helper pointer
/// @return Success or LogError on failure
LogOutcome make_log_amendment_file_helper(
  jewels::memory::MemoryResource memory_resource,
  const LogUri& log_metadata_uri,
  const std::pmr::unordered_set<std::string_view>& excluded_channels,
  bool is_merge_union,
  ChunkReaderWriterFactory<>& chunk_reader_factory,
  jewels::Out<std::shared_ptr<LogMetadataHelperInterface>> helper_ptr);

/// Helper function to make a log metadata file helper given a log URI
/// @param[in] memory_resource Memory resource
/// @param[in] log_metadata_uri Log metadata file file URI
/// @param[in] excluded_channels Channels to exclude when reading the log
/// @param[in] chunk_reader_factory Chunk reader factory
/// @param[out] helper_ptr Log metadata helper pointer
/// @return Success or LogError on failure
LogOutcome make_log_metadata_file_helper(
  jewels::memory::MemoryResource memory_resource,
  const LogUri& log_metadata_uri,
  const std::pmr::unordered_set<std::string_view>& excluded_channels,
  ChunkReaderWriterFactory<>& chunk_reader_factory,
  jewels::Out<std::shared_ptr<LogMetadataHelperInterface>> helper_ptr);

/// Helper function to make a log metadata helper given a log URI
/// @param[in] memory_resource Memory resource
/// @param[in] log_uri Log URI
/// @param[in] excluded_channels Channels to exclude from the kog
/// @param[in] is_merge_union True if the log metadata was created by 'make_merge_union'
/// @param[in] chunk_reader_factory Chunk reader factory
/// @param[out] helper_ptr Log metadata helper pointer
/// @return Success or LogError on failure
LogOutcome make_log_metadata_helper(
  jewels::memory::MemoryResource memory_resource,
  const LogUri& log_uri,
  const std::pmr::unordered_set<std::string_view>& excluded_channels,
  bool is_merge_union,
  ChunkReaderWriterFactory<>& chunk_reader_factory,
  jewels::Out<std::shared_ptr<LogMetadataHelperInterface>> helper_ptr);

/// Get the URI for a log path (possibly relative to the log_uri)
/// @param[in] memory_resource Memory resource
/// @param[in] log_uri Log URI
/// @param[in] log_path Log path
/// @param[out] path_uri Log path URI
/// @param[out] excluded_channels Channels to exclude from the log
/// @return Success or LogError on failure
[[nodiscard]] LogOutcome get_log_path_uri(
  jewels::memory::MemoryResource memory_resource,
  const LogUri& log_uri,
  const ::clockwork::logging::offboard::v1::LogPath& log_path,
  jewels::Out<LogUri> path_uri,
  jewels::Out<std::pmr::unordered_set<std::string_view>> excluded_channels);

} // namespace clockwork_logging::offboard
