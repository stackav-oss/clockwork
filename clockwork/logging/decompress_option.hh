// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <wise_enum.h>

#include <cstdint>
#include <ostream>

namespace clockwork_logging
{

/// Decompression option when reading logs that contain lite-compressed messages
WISE_ENUM_CLASS(
  (DecompressOption, uint8_t),
  // Decompress messages as they are read
  decompress,
  // Don't decompress messages as they are read
  dont_decompress)

/// Output stream insertion operator for decompress option
/// @param[in] ostream Output stream
/// @param[in] value Enum value
/// @return Output stream reference
inline std::ostream& operator<<(std::ostream& ostream, DecompressOption value);

} // namespace clockwork_logging

#include "clockwork/logging/decompress_option.inl"
