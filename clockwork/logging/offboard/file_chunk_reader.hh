// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/offboard/chunk_reader.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/filesystem/filesystem.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"

#include <cstddef>
#include <string_view>
#include <vector>

namespace clockwork_logging::offboard
{

/// Chunk reader implementation to regular files
///
/// @tparam FilesystemType Filesystem type
template <typename FilesystemType = jewels::filesystem::Filesystem>
class FileChunkReader : public ChunkReader
{
public:
  /// Don't call constructor directly, use make_shared to create an instance
  /// @param[in] file_uri Log file URI
  /// @param[in] memory_resource Memory resource
  FileChunkReader(LogUri file_uri, jewels::memory::MemoryResource memory_resource);

  ~FileChunkReader() override = default;

  FileChunkReader(const FileChunkReader& other) = delete;
  FileChunkReader& operator=(const FileChunkReader& other) = delete;
  FileChunkReader(FileChunkReader&&) noexcept = default;
  FileChunkReader& operator=(FileChunkReader&&) noexcept = default;

  /// Create a shared pointer to a file chunk reader
  /// @param[in] file_uri Log file URI
  /// @param[in] memory_resource Memory resource
  /// Pointer to the file chunk reader or LogError on failure
  [[nodiscard]] static LogExpected<jewels::memory::NonNullSharedPtr<FileChunkReader>>
  make_shared(std::string_view file_uri, const jewels::memory::MemoryResource& memory_resource);

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

  /// Filesystem accessor
  [[nodiscard]] FilesystemType& filesystem() noexcept;

private:
  /// Log file URI
  LogUri file_uri_;

  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;

  /// Filesystem used to read the log file
  FilesystemType filesystem_;

  /// Log file descriptor
  jewels::filesystem::FileDescriptor file_desc_;

  /// Flag set when the reader has been closed
  bool is_closed_{false};
};

} // namespace clockwork_logging::offboard

#include "clockwork/logging/offboard/file_chunk_reader.inl"
