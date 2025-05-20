// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/offboard/chunk_reader.hh"
#include "clockwork/logging/offboard/chunk_writer.hh"
#include "clockwork/logging/offboard/log_uri.hh"
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
  [[nodiscard]] LogExpected<bool> exists(const LogUri& file_uri) const;

  /// Create the directories for a log file URI
  /// @param[in] file_uri Log file URI
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> create_directories(const LogUri& file_uri) const;

  /// Get the log files found under a log URI
  /// @param[in] uri_str Log URI
  /// @return Vector of log file paths or LogError on failure
  [[nodiscard]] LogExpected<std::pmr::vector<std::pmr::string>> list_log_files(const LogUri& file_uri) const;

  /// Write a file to a log
  /// @param[in] file_uri Log file URI
  /// @param[in] data Data to write
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> write_log_file(const LogUri& file_uri, std::span<const std::byte> data) const;

  /// Read a file from a log
  /// @param[in] file_uri Log file URI
  /// @return File data or LogError on failure
  [[nodiscard]] LogExpected<std::pmr::vector<std::byte>> read_log_file(const LogUri& file_uri) const;

private:
  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;
};

} // namespace clockwork_logging::offboard
