// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/offboard/chunk_compressor.hh"
#include "clockwork/logging/offboard/chunk_reader.hh"
#include "clockwork/logging/offboard/log_format.hh"
#include "clockwork/logging/offboard/reader_types.hh"
#include "jewels/memory/memory_resource.hh"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace clockwork_logging::offboard
{

/// Read the metadata chunk and return the metadata for the channels we want to read from the log
/// @param[in] memory_resource Memory resource
/// @param[in] metadata_location Metadata chunk location
/// @param[in] channel_info_map Map from channel ID to logged channel info
/// @param[in] chunk_reader Chunk reader
/// @param[in] chunk_compressor Chunk compressor
/// @return Map from channel ID to logged channel info or LogError on failure
[[nodiscard]] LogExpected<std::pmr::unordered_map<uint16_t, reader::LoggedChannelInfo>> read_metadata_chunk(
  jewels::memory::MemoryResource memory_resource,
  ChunkLocation metadata_location,
  ChunkReader& chunk_reader,
  ChunkCompressor& chunk_compressor);

} // namespace clockwork_logging::offboard
