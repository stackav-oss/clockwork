// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/offboard/chunk_reader.hh"
#include "clockwork/logging/offboard/chunk_writer.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"

#include <aws/core/client/ClientConfiguration.h>
#include <aws/s3/S3Client.h>

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace clockwork_logging::offboard
{

/// Factory to create S3 chunk readers and writers
///
/// This class also implements some backing store independent implementations of utility
/// functions needed to read and write logs.
class S3ChunkReaderWriterFactory
{
public:
  /// Constructor
  /// @param[in] memory_resource Memory resource
  explicit S3ChunkReaderWriterFactory(jewels::memory::MemoryResource memory_resource);

  ~S3ChunkReaderWriterFactory() = default;

  S3ChunkReaderWriterFactory(const S3ChunkReaderWriterFactory& other) = delete;
  S3ChunkReaderWriterFactory& operator=(const S3ChunkReaderWriterFactory& other) = delete;
  S3ChunkReaderWriterFactory(S3ChunkReaderWriterFactory&&) noexcept = default;
  S3ChunkReaderWriterFactory& operator=(S3ChunkReaderWriterFactory&&) noexcept = default;

  /// Create a shared pointer to a file chunk reader
  /// @param[in] file_uri Log URI
  /// Pointer to the file chunk reader or LogError on failure
  [[nodiscard]] LogExpected<jewels::memory::NonNullSharedPtr<ChunkReader>> make_chunk_reader(const LogUri& file_uri);

  /// Create a shared pointer to a file chunk writer
  /// @param[in] file_uri Log URI
  /// Pointer to the file chunk reader or LogError on failure
  [[nodiscard]] LogExpected<jewels::memory::NonNullSharedPtr<ChunkWriter>> make_chunk_writer(const LogUri& file_uri);

  /// Test whether a file exists
  /// @param[in] file_uri Log file URI
  /// @return True iff an object exists at the log URI or LogError on failure
  [[nodiscard]] LogExpected<bool> exists(const LogUri& file_uri);

  /// Get the log files found under a log URI
  /// @param[in] uri_str Log URI
  /// @return Vector of log file paths or LogError on failure
  [[nodiscard]] LogExpected<std::pmr::vector<std::pmr::string>> list_log_files(const LogUri& file_uri);

  /// Write a file to a log
  /// @param[in] file_uri Log file URI
  /// @param[in] data Data to write
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> write_log_file(const LogUri& file_uri, std::span<const std::byte> data);

  /// Read a file from a log
  /// @param[in] file_uri Log file URI
  /// @return File data or LogError on failure
  [[nodiscard]] LogExpected<std::pmr::vector<std::byte>> read_log_file(const LogUri& file_uri);

private:
  /// Get the S3 client pointer
  /// The pointer is created the first time through
  /// @return S3 client pointer
  [[nodiscard]] jewels::memory::NonNullSharedPtr<Aws::S3::S3Client> get_s3_client_ptr();

  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;

  /// S3 client pointer
  std::shared_ptr<Aws::S3::S3Client> s3_client_ptr_;

  /// AWS client configuration pointer
  std::shared_ptr<Aws::Client::ClientConfiguration> aws_client_config_ptr_;
};

} // namespace clockwork_logging::offboard
