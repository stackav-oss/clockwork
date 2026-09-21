// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/offboard/chunk_writer.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "clockwork/logging/offboard/s3_utils_interface.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"

#include <aws/s3/model/CompletedPart.h>

#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace clockwork_logging::offboard
{

/// Chunk writer implementation to write to S3 files
class S3ChunkWriter : public ChunkWriter
{
public:
  /// Do not call constructor directly, use make_shared to create an instance
  /// @param[in] memory_resource Memory resource
  /// @param[in] file_uri Log file URI
  /// @param[in] s3_utils_ptr S3 utils pointer
  S3ChunkWriter(
    jewels::memory::MemoryResource memory_resource,
    LogUri file_uri,
    jewels::memory::NonNullSharedPtr<S3UtilsInterface> s3_utils_ptr);

  /// Destructor warns if not closed cleanly
  ~S3ChunkWriter() override;

  S3ChunkWriter(const S3ChunkWriter& other) = delete;
  S3ChunkWriter& operator=(const S3ChunkWriter& other) = delete;
  S3ChunkWriter(S3ChunkWriter&&) noexcept = delete;
  S3ChunkWriter& operator=(S3ChunkWriter&&) noexcept = delete;

  /// Create a shared pointer to a S3 chunk writer
  /// @param[in] memory_resource Memory resource
  /// @param[in] file_uri Log file URI
  /// @param[in] s3_utils_ptr S3 utils pointer
  /// Pointer to the S3 chunk writer or LogError on failure
  [[nodiscard]] static LogExpected<jewels::memory::NonNullSharedPtr<S3ChunkWriter>> make_shared(
    const jewels::memory::MemoryResource& memory_resource,
    std::string_view file_uri,
    const jewels::memory::NonNullSharedPtr<S3UtilsInterface>& s3_utils_ptr);

  /// @see ChunkWriter::file_uri
  [[nodiscard]] const LogUri& file_uri() const noexcept override;

  /// @see ChunkWriter::open
  [[nodiscard]] LogExpected<void> open() override;

  /// @see ChunkWriter::get_file_size
  [[nodiscard]] LogExpected<size_t> get_file_size() const override;

  /// @see ChunkWriter::write_chunk
  [[nodiscard]] LogExpected<size_t> write_chunk(std::pmr::vector<std::byte> data) override;

  /// @see ChunkWriter::close
  [[nodiscard]] LogExpected<ChunkWriter::WriteMetrics> close() override;

private:
  /// Upload pending write buffers
  /// @param[in] guard Unique lock holding mutex_
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> upload_pending_buffer(std::unique_lock<std::mutex>& guard);

  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;

  /// Log file uri
  LogUri file_uri_;

  /// S3 client pointer
  jewels::memory::NonNullSharedPtr<S3UtilsInterface> s3_utils_ptr_;

  /// Upload ID for multipart upload
  std::pmr::string upload_id_;

  /// Next upload part number
  int32_t next_part_number_{1};

  /// Buffer waiting to be uploaded
  std::pmr::vector<std::byte> pending_buffer_;

  /// Number of bytes waiting to be uploaded
  size_t pending_buffer_bytes_{};

  /// Vector of completed parts
  std::pmr::vector<Aws::S3::Model::CompletedPart> completed_parts_;

  /// Mutex to serialize access to this object
  mutable std::mutex mutex_;

  /// Flag set when the writer has been opened
  bool is_open_{false};

  /// Flag set when the writer has been closed
  bool is_closed_{false};

  /// Next chunk offset in bytes
  size_t next_chunk_offset_{0U};

  /// Write metrics
  ChunkWriter::WriteMetrics write_metrics_{};
};

} // namespace clockwork_logging::offboard
