// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/offboard/chunk_compressor.hh"
#include "clockwork/logging/offboard/chunk_reader.hh"
#include "clockwork/logging/offboard/chunk_writer.hh"
#include "clockwork/logging/offboard/log_format.hh"
#include "clockwork/logging/offboard/reader_types.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"

namespace clockwork_logging::offboard
{

/// Write the log file trailer chunk to a log file
/// @param[in] memory_resource Memory resource
/// @param[in] metadata_chunk_location Metadata chunk location
/// @param[in] metrics_chunk_location Metrics chunk location
/// @param[in] index_chunk_location Index chunk location
/// @param[in] chunk_writer Chunk writer
/// @param[in] chunk_compressor Chunk compressor
/// @return LogError on failure
[[nodiscard]] LogExpected<void> write_log_file_trailer(
  jewels::memory::MemoryResource memory_resource,
  const ChunkLocation& metadata_chunk_location,
  const ChunkLocation& metrics_chunk_location,
  const ChunkLocation& index_chunk_location,
  ChunkWriter& chunk_writer,
  const ChunkCompressor& chunk_compressor);

/// Read the log file trailer chunk from a log file
/// @param[in] chunk_reader_ptr Chunk reader pointer
/// @param[in] chunk_compressor_ptr Chunk compressor pointer
/// @return Log file trailer contents or LogError on failure
[[nodiscard]] LogExpected<reader::LogFileTrailerInfo> read_log_file_trailer(
  const jewels::memory::NonNullSharedPtr<ChunkReader>& chunk_reader_ptr,
  const jewels::memory::NonNullSharedPtr<ChunkCompressor>& chunk_compressor_ptr);

} // namespace clockwork_logging::offboard
