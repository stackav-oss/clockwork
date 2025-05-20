// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <wise_enum.h>

#include <cstdint>
namespace jewels
{

/// Log level
WISE_ENUM_CLASS(
  (LogLevel, uint8_t),
  // Debug log level
  (debug, 1U),
  // Info log level
  (info, 2U),
  // Warning log level
  (warn, 3U),
  // Error log level
  (error, 4U),
  // Fatal log level
  (fatal, 5U))

} // namespace jewels
