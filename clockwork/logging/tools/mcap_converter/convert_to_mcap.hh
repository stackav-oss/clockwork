// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string_view>

namespace clockwork_logging
{

/// Convert a log to MCAP log format
/// @param[in] source_path Source log path
/// @param[in] mcap_path MCAP log path
/// @throws runtime_error on failure
void convert_to_mcap(std::string_view source_path, std::string_view mcap_path);

} // namespace clockwork_logging
