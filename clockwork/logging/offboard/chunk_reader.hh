// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/offboard/log_uri.hh"

#include <cstddef>
#include <vector>

namespace clockwork_logging::offboard
{

/// Interface implemented by classes that read from log files in chunks
///
/// The chunk reader reads the chunks from the file and returns a vector of bytes containing
/// the chunk data.
///
/// Reading beyond the end of the log file is an error.
class ChunkReader
{
public:
  ChunkReader() noexcept = default;
  virtual ~ChunkReader() noexcept = default;

  ChunkReader(const ChunkReader& other) = delete;
  ChunkReader& operator=(const ChunkReader& other) = delete;
  ChunkReader(ChunkReader&&) noexcept = default;
  ChunkReader& operator=(ChunkReader&&) noexcept = default;

  /// Accessor for the log file URI
  [[nodiscard]] virtual const LogUri& file_uri() const noexcept = 0;

  /// Open the log file for reading
  /// @return LogError on failure
  [[nodiscard]] virtual LogExpected<void> open() = 0;

  /// Returns the size of the log file in bytes
  /// @return File size or LogError on failure
  [[nodiscard]] virtual LogExpected<size_t> file_size() = 0;

  /// Read a chunk from the log file
  /// @param[in] offset Chunk offset
  /// @param[in] length Chunk length in bytes
  /// @return LogError on failure
  [[nodiscard]] virtual LogExpected<std::pmr::vector<std::byte>> read_chunk(size_t offset, size_t length) = 0;

  /// Close the chunk reader
  /// @post The chunk reader cannot be reopened.
  /// @post Future attempts to read chunks will fail with log_not_open
  /// @return LogError on failure
  [[nodiscard]] virtual LogExpected<void> close() = 0;
};

} // namespace clockwork_logging::offboard
