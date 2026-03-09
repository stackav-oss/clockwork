// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/offboard/chunk_reader.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "clockwork/logging/offboard/s3_utils_interface.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"

#include <cstddef>
#include <string_view>
#include <vector>

namespace clockwork_logging::offboard
{

/// Chunk reader implementation for S3 files
class S3ChunkReader : public ChunkReader
{
public:
  /// Don't call constructor directly, use make_shared to create an instance
  /// @param[in] memory_resource Memory resource
  /// @param[in] file_uri Log file URI
  /// @param[in] s3_utils_ptr S3 utils pointer
  S3ChunkReader(
    jewels::memory::MemoryResource memory_resource,
    LogUri file_uri,
    const jewels::memory::NonNullSharedPtr<S3UtilsInterface>& s3_utils_ptr);

  ~S3ChunkReader() override = default;

  S3ChunkReader(const S3ChunkReader& other) = delete;
  S3ChunkReader& operator=(const S3ChunkReader& other) = delete;
  S3ChunkReader(S3ChunkReader&&) noexcept = default;
  S3ChunkReader& operator=(S3ChunkReader&&) noexcept = default;

  /// Create a shared pointer to a file chunk reader
  /// @param[in] memory_resource Memory resource
  /// @param[in] file_uri Log file URI
  /// @param[in] s3_utils_ptr S3 utils pointer
  /// Pointer to the file chunk reader or LogError on failure
  [[nodiscard]] static LogExpected<jewels::memory::NonNullSharedPtr<S3ChunkReader>> make_shared(
    const jewels::memory::MemoryResource& memory_resource,
    std::string_view file_uri,
    const jewels::memory::NonNullSharedPtr<S3UtilsInterface>& s3_utils_ptr);

  /// @see ChunkReader::file_uri
  [[nodiscard]] const LogUri& file_uri() const noexcept override;

  /// @see ChunkReader::open
  [[nodiscard]] LogExpected<void> open() override;

  /// @see ChunkReader::file_size
  [[nodiscard]] LogExpected<size_t> file_size() override;

  /// @see ChunkReader::read_chunk
  [[nodiscard]] LogExpected<std::pmr::vector<std::byte>> read_chunk(size_t offset, size_t length) override;

  /// @see ChunkReader::close
  [[nodiscard]] LogExpected<void> close() override;

private:
  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;

  /// S3 file URI
  LogUri file_uri_;

  /// S3 utils pointer
  jewels::memory::NonNullSharedPtr<S3UtilsInterface> s3_utils_ptr_;

  /// Flag set when the reader has been opened
  bool is_open_{false};

  /// Flag set when the reader has been closed
  bool is_closed_{false};
};

} // namespace clockwork_logging::offboard
