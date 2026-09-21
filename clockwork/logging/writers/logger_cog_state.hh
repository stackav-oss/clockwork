// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/writers/logger.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"

namespace clockwork_logging
{

/// Logger cog state
struct LoggerCogState
{
  /// Memory resource
  jewels::memory::MemoryResource memory_resource;

  /// Telemetry writer pointer
  jewels::memory::pmr_unique_ptr<Logger> logger_ptr;
};

} // namespace clockwork_logging
