// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// min, max, clamp, etc.
#include <au/math.hh> // IWYU pragma: export

namespace au
{
/// Linearly interpolates between start and end using ratio as the interpolation factor.
template <typename Start, typename End, typename Ratio>
constexpr auto lerp(Start start, End end, Ratio ratio)
{
  return start + ratio * (end - start);
}
} // namespace au
