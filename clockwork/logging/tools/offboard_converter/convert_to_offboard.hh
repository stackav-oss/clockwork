// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string_view>

namespace clockwork_logging
{

/// Convert a log to offboard log format
/// @param[in] source_path Source log path
/// @param[in] offboard_path Offboard log path
/// @param[in] writer_config_pbtxt Offboard log writer config text protobuf string
/// @throws runtime_error on failure
void convert_to_offboard(
  std::string_view onboard_path, std::string_view offboard_path, std::string_view writer_config_pbtxt = "");

} // namespace clockwork_logging
