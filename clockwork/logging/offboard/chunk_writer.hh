// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/offboard/log_uri.hh"

#include <chrono>
#include <cstddef>
#include <vector>

namespace clockwork_logging::offboard
{

/// Interface implemented by classes that write to log files in chunks
///
/// When a chunk is written, the chunk writer takes ownership of the buffer and writes
/// the chunk to the log file.
///
/// If the backing store has a minimum chunk size the writer may defer writing to combine
/// multiple chunks until it has enough data for a full chunk.
class ChunkWriter
{
public:
  /// File write metrics
  struct WriteMetrics
  {
    /// Bytes written
    size_t byte_count{0U};

    /// Number of writes
    size_t write_count{0U};

    /// Total write latency
    std::chrono::nanoseconds write_latency{0};

    /// += operator for accumulating metrics from multiple log files
    constexpr WriteMetrics& operator+=(const WriteMetrics& rhs)
    {
      byte_count += rhs.byte_count;
      write_count += rhs.write_count;
      write_latency += rhs.write_latency;
      return *this;
    }
  };

  ChunkWriter() noexcept = default;
  virtual ~ChunkWriter() noexcept = default;

  ChunkWriter(const ChunkWriter& other) = delete;
  ChunkWriter& operator=(const ChunkWriter& other) = delete;
  ChunkWriter(ChunkWriter&&) noexcept = default;
  ChunkWriter& operator=(ChunkWriter&&) noexcept = default;

  /// Accessor for the log file URI
  [[nodiscard]] virtual const LogUri& file_uri() const noexcept = 0;

  /// Open the log file for writing
  /// @return LogError on failure
  [[nodiscard]] virtual LogExpected<void> open() = 0;

  /// Get the current file size in bytes
  /// @return File size or LogError on failure
  [[nodiscard]] virtual LogExpected<size_t> get_file_size() const = 0;

  /// Write a chunk to the log file at the next chunk offset
  /// @param[in] data Chunk data to be written
  /// @return File offset where the chunk was written or LogError on failure
  [[nodiscard]] virtual LogExpected<size_t> write_chunk(std::pmr::vector<std::byte> data) = 0;

  /// Commit all pending chunks to the log file and close the chunk writer
  /// @post The chunk writer cannot be reopened.
  /// @post Future attempts to write new chunks will fail with log_not_open
  /// @return Write metrics LogError on failure
  [[nodiscard]] virtual LogExpected<WriteMetrics> close() = 0;
};

} // namespace clockwork_logging::offboard
