// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <iterator>

namespace clockwork::pinion
{

/// Index that maps to an index in the buffer.
/// @note BufferIndex modulo number of slots is the position within
/// the buffer.
using BufferIndex = uint64_t;

} // namespace clockwork::pinion
