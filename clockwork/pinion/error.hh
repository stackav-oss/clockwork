// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <wise_enum.h>

#include <cstdint>

namespace clockwork::pinion
{

/// Errors during initialization of a buffer.
WISE_ENUM_CLASS((InitError, uint8_t), invalid_size, invalid_alignment)

/// Errors when writing to a buffer.
WISE_ENUM_CLASS(
  (WriteError, uint8_t), unexpected_reservation, unexpected_head, count_too_large, seqno_count_mismatch, non_contiguous)

/// Errors when reserving write space.
WISE_ENUM_CLASS((ReserveError, uint8_t), unexpected_tail, existing_reservation, count_too_large)

/// Error for a subscriber falling behind.
WISE_ENUM_CLASS((ProgressError, uint8_t), fell_behind, in_the_future)

/// Error for creating a publisher handle.
WISE_ENUM_CLASS((PublisherError, uint8_t), non_zeroed_buffer)

} // namespace clockwork::pinion
