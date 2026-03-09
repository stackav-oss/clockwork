// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/offboard/chunk_reader_writer_factory.hh"
#include "clockwork/logging/offboard/log_metadata_helper_interface.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "jewels/memory/memory_resource.hh"

#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace clockwork_logging::offboard
{

/// Helper function to make a log metadata helper given a log URI
/// @param[in] memory_resource Memory resource
/// @param[in] log_uri Log URI
/// @param[in] chunk_reader_factory Chunk reader factory
/// @return Pointer to a log metadata helper or LogError on failure
[[nodiscard]] LogExpected<std::shared_ptr<LogMetadataHelperInterface>> make_log_metadata_helper(
  jewels::memory::MemoryResource memory_resource,
  const LogUri& log_uri,
  ChunkReaderWriterFactory<>& chunk_reader_factory);

} // namespace clockwork_logging::offboard
