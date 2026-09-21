// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/readers/types.hh"
#include "jewels/memory/memory_resource.hh"

#include <functional>      // IWYU pragma: keep
#include <memory_resource> // IWYU pragma: keep
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>

namespace clockwork_logging::offboard
{

/// Create a union of the logs to be merged, removing duplicates so that each message is read exactly once.
/// @param[in] memory_resource Memory resource
/// @param[in] source_uris Source log URIs
/// @param[in] output_path Union output path
/// @return LogError on failure
[[nodiscard]] LogExpected<void> write_merge_union(
  jewels::memory::MemoryResource memory_resource,
  std::span<std::string_view> source_uris,
  std::string_view output_path);

/// Merges the source offboard format logs to a new log in the offboard log format
/// @param[in] memory_resource Memory resource
/// @param[in] source_uris Source log URIs
/// @param[in] dest_uri Destination log URI
/// @param[in] maybe_desired_channels Optional set of desired channels
/// @param[in] maybe_log_interval Optional relative log interval
/// @param[in] writer_config_str Writer config protobuf string
/// @return LogError on failure
[[nodiscard]] LogExpected<void> merge_logs(
  jewels::memory::MemoryResource memory_resource,
  std::span<std::string_view> source_uris,
  std::string_view dest_uri,
  const std::optional<std::pmr::unordered_set<std::pmr::string>>& maybe_desired_channels = std::nullopt,
  const std::optional<RelativeInterval>& maybe_log_interval = std::nullopt,
  std::string_view writer_config_str = {});

} // namespace clockwork_logging::offboard
