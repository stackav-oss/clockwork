// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/offboard/chunk_writer.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/filesystem/filesystem.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"

#include <cstddef>
#include <mutex>
#include <string_view>
#include <vector>

namespace clockwork_logging::offboard
{

/// Chunk writer implementation to regular files
/// @tparam FilesystemType Filesystem type
template <typename FilesystemType = jewels::filesystem::Filesystem>
class FileChunkWriter : public ChunkWriter
{
public:
  /// Do not call constructor directly, use make_shared to create an instance
  /// @param[in] file_uri Log file URI
  /// @param[in] memory_resource Memory resource
  FileChunkWriter(LogUri file_uri, jewels::memory::MemoryResource memory_resource);

  /// Destructor warns if not closed cleanly
  ~FileChunkWriter() override;

  FileChunkWriter(const FileChunkWriter& other) = delete;
  FileChunkWriter& operator=(const FileChunkWriter& other) = delete;
  FileChunkWriter(FileChunkWriter&&) noexcept = default;
  FileChunkWriter& operator=(FileChunkWriter&&) noexcept = default;

  /// Create a shared pointer to a file chunk writer
  /// @param[in] file_uri Log file URI
  /// @param[in] memory_resource Memory resource
  /// Pointer to the file chunk writer or LogError on failure
  [[nodiscard]] static LogExpected<jewels::memory::NonNullSharedPtr<FileChunkWriter>>
  make_shared(std::string_view file_uri, const jewels::memory::MemoryResource& memory_resource);

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

  /// Filesystem accessor
  [[nodiscard]] FilesystemType& filesystem() noexcept;

private:
  /// Log file uri
  LogUri file_uri_;

  /// Filesystem used to write the log file
  FilesystemType filesystem_;

  /// Log file descriptor
  jewels::filesystem::FileDescriptor file_desc_;

  /// Current file offset in bytes
  size_t current_offset_{0U};

  /// Flag set when the writer has been closed
  bool is_closed_{false};

  /// Mutex to serialize access to current_offset_
  mutable std::mutex mutex_;

  /// Write metrics
  ChunkWriter::WriteMetrics write_metrics_{};
};

} // namespace clockwork_logging::offboard

#include "clockwork/logging/offboard/file_chunk_writer.inl"
