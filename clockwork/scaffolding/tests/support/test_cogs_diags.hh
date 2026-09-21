// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <chrono>
#include <cstddef>

namespace clockwork::testing
{
constexpr auto init_cog_1_duration_threshold = std::chrono::microseconds(1000);
constexpr auto test_cog_1_duration_threshold = std::chrono::microseconds(1000);
constexpr size_t test_cog_1_out_a_frequency_window = 10;
constexpr double test_cog_1_out_a_frequency_min_hz = 8;
constexpr double test_cog_1_out_a_frequency_max_hz = 12;
constexpr auto test_cog_2_duration_threshold = std::chrono::microseconds(1000);
constexpr auto test_cog_2_in_a_missing_threshold = std::chrono::milliseconds(100);
constexpr auto test_cog_2_in_a_skip_count_threshold = 2UL;
constexpr size_t test_cog_2_out_a_frequency_window = 10;
constexpr double test_cog_2_out_a_frequency_min_hz = 8;
constexpr double test_cog_2_out_a_frequency_max_hz = 12;
} // namespace clockwork::testing
