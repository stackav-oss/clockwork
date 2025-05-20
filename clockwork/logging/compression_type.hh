// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <wise_enum.h>

#include <cstdint>
#include <ostream>

namespace clockwork_logging
{

/// Compression type
WISE_ENUM_CLASS(
  // NOLINTNEXTLINE(performance-enum-size) This enum is used in logs and the size is fixed.
  (CompressionType, uint16_t),
  // Not compressed
  (none, 1U),
  // Compressed with ZSTD
  (zstd, 2U))

/// Output stream insertion operator for compression type
/// @param[in] ostream Output stream
/// @param[in] value Enum value
/// @return Output stream reference
inline std::ostream& operator<<(std::ostream& ostream, CompressionType value);

} // namespace clockwork_logging

#include "clockwork/logging/compression_type.inl"
