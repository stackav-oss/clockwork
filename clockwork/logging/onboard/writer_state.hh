// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <wise_enum.h>

#include <cstdint>
#include <ostream>

namespace clockwork_logging::onboard
{

/// Writer state
WISE_ENUM_CLASS(
  (WriterState, uint8_t),
  // Closed
  closed,
  // Paused
  paused,
  // Logging
  logging,
  // Logging, but in a degraded state, status string contains descriptive reason
  degraded,
  // Failed, status string contains descriptive reason
  failed)

/// Output stream insertion operator for log writer state
/// @param[in] ostream Output stream
/// @param[in] value Enum value
/// @return Output stream reference
inline std::ostream& operator<<(std::ostream& ostream, WriterState value);

} // namespace clockwork_logging::onboard

#include "clockwork/logging/onboard/writer_state.inl"
