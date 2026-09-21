// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstdint>
#include <ratio>

namespace clockwork
{
// Define the denominator for 10 nanoseconds (1/100,000,000 of a second)
constexpr std::uint64_t ten_ns_denominator = 100000000;

// Custom duration type for 10 nanosecond precision timing
using ten_ns_rep = uint32_t;                             // underlying type
using ten_ns_period = std::ratio<1, ten_ns_denominator>; // 10 nanoseconds (1/100,000,000 of a second)
using TenNanoseconds = std::chrono::duration<ten_ns_rep, ten_ns_period>;

// This factory is intended to be used for the corresponding clockwork strong type.
// @param value The number of 10ns units
// @return A TenNanoseconds std::chrono object initialized with the given value
TenNanoseconds ten_nanoseconds_factory(uint32_t value);
} // namespace clockwork
