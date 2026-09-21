// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/offboard/chunk_reader.hh"
#include "clockwork/logging/offboard/chunk_writer.hh"
#include "clockwork/logging/offboard/log_format.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "jewels/filesystem/filesystem.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace clockwork_logging::offboard
{

/// Factory to create file chunk readers and writers
///
/// This class also implements some backing store independent implementations of utility
/// functions needed to read and write logs.
///
/// @tparam FilesystemType Class used to access the filesystem
template <typename FilesystemType = jewels::filesystem::Filesystem>
class FileChunkReaderWriterFactory
{
public:
  /// Constructor
  /// @param[in] memory_resource Memory resource
  explicit FileChunkReaderWriterFactory(jewels::memory::MemoryResource memory_resource);

  ~FileChunkReaderWriterFactory() = default;

  FileChunkReaderWriterFactory(const FileChunkReaderWriterFactory& other) = delete;
  FileChunkReaderWriterFactory& operator=(const FileChunkReaderWriterFactory& other) = delete;
  FileChunkReaderWriterFactory(FileChunkReaderWriterFactory&&) noexcept = default;
  FileChunkReaderWriterFactory& operator=(FileChunkReaderWriterFactory&&) noexcept = default;

  /// Filesystem accessor
  /// @return Instance used to access the filesystem
  [[nodiscard]] FilesystemType& get_filesystem();

  /// Create a shared pointer to a file chunk reader
  /// @param[in] file_uri Log URI
  /// Pointer to the file chunk reader or LogError on failure
  [[nodiscard]] LogExpected<jewels::memory::NonNullSharedPtr<ChunkReader>>
  make_chunk_reader(const LogUri& file_uri) const;

  /// Create a shared pointer to a file chunk writer
  /// @param[in] file_uri Log URI
  /// Pointer to the file chunk reader or LogError on failure
  [[nodiscard]] LogExpected<jewels::memory::NonNullSharedPtr<ChunkWriter>>
  make_chunk_writer(const LogUri& file_uri) const;

  /// Test whether a file exists
  /// @param[in] file_uri Log file URI
  /// @return True iff an object exists at the log URI or LogError on failure
  [[nodiscard]] LogExpected<bool> exists(const LogUri& file_uri);

  /// Get the size of a file in bytes
  /// @param[in] file_uri Log file URI
  /// @return File size in bytes or LogError on failure
  [[nodiscard]] LogExpected<size_t> get_size(const LogUri& file_uri);

  /// Create the directories for a log file URI
  /// @param[in] file_uri Log file URI
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> create_directories(const LogUri& file_uri);

  /// Get the log files found under a log URI
  /// @param[in] uri_str Log URI
  /// @param[in] suffix Log file suffix
  /// @return Vector of log file paths or LogError on failure
  [[nodiscard]] LogExpected<std::pmr::vector<LogUri>>
  list_log_files(const LogUri& file_uri, std::string_view suffix = log_file_suffix);

  /// Get the subdirectories found under a log URI
  /// @param[in] uri_str Log URI
  /// @return Vector of log file paths or LogError on failure
  [[nodiscard]] LogExpected<std::pmr::vector<LogUri>> list_subdirs(const LogUri& file_uri);

  /// Write a file to a log
  /// @param[in] file_uri Log file URI
  /// @param[in] data Data to write
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> write_log_file(const LogUri& file_uri, std::span<const std::byte> data);

  /// Read a file from a log
  /// @param[in] file_uri Log file URI
  /// @return File data or LogError on failure
  [[nodiscard]] LogExpected<std::pmr::vector<std::byte>> read_log_file(const LogUri& file_uri);

  /// Read a file from a log
  /// @param[in] file_uri Log file URI
  /// @param[in] offset File offset
  /// @param[in] buffer_span Buffer used to read the data
  /// @return File data span or LogError on failure
  [[nodiscard]] LogExpected<std::span<std::byte>>
  read_log_file(const LogUri& file_uri, size_t offset, std::span<std::byte> buffer_span);

private:
  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;

  /// Filesystem interface instance
  FilesystemType filesystem_;
};

} // namespace clockwork_logging::offboard

#include "clockwork/logging/offboard/file_chunk_reader_writer_factory.inl"
