// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <mcap/writer.hpp>

namespace clockwork_logging
{

/// Create the MCAP writer options used to write on offboard log
/// @return MCAP writer options
[[nodiscard]] mcap::McapWriterOptions make_offload_mcap_writer_options();

} // namespace clockwork_logging
