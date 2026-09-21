// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/offboard/chunk_compressor.hh"
#include "clockwork/logging/offboard/chunk_reader.hh"
#include "clockwork/logging/offboard/log_format.hh"
#include "clockwork/logging/offboard/reader_types.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"

#include <cstdint>
#include <list>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace clockwork_logging::offboard
{

/// Read the chunk index from a log file and return a list of message chunk handles
/// for the chunks we need to read from the log
/// @param[in] memory_resource Memory resource
/// @param[in] index_location Index chunk location
/// @param[in] channel_info_map Map from channel ID to logged channel info
/// @param[in] channel_ids_to_exclude Set of channel IDs to exclude
/// @param[in] maybe_log_interval Optional interval to be read from the log
/// @param[in] desired_channels Set of channels to be read from the log
/// @param[in] chunk_reader_ptr Chunk reader pointer
/// @param[in] chunk_compressor_ptr Chunk compressor pointer
/// @return List of message chunk handles or LogError on failure
[[nodiscard]] LogExpected<std::pmr::list<reader::MessageChunkHandle>> read_index_chunk(
  jewels::memory::MemoryResource memory_resource,
  ChunkLocation index_location,
  const std::pmr::unordered_map<uint16_t, reader::LoggedChannelInfo>& channel_info_map,
  const std::pmr::unordered_set<uint16_t>& channel_ids_to_exclude,
  const std::optional<LogInterval>& maybe_log_interval,
  const std::pmr::unordered_set<std::pmr::string>& desired_channels,
  const jewels::memory::NonNullSharedPtr<ChunkReader>& chunk_reader_ptr,
  const jewels::memory::NonNullSharedPtr<ChunkCompressor>& chunk_compressor_ptr);

} // namespace clockwork_logging::offboard
