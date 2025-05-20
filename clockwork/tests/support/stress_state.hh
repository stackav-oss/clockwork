// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/memory/memory_resource.hh"

#include <array>
#include <memory>

namespace clockwork::tests
{

/// Number of stress test cogs
static constexpr size_t num_stress_cogs = 5U;

/// Stress test counters
struct StressCounters
{
};

/// Stress test state
struct StressState
{
  /// Memory resource
  jewels::memory::MemoryResource memory_resource;

  /// Reporter cog execution counter
  size_t reporter_counter{};

  /// Cog execution counters (0 is the subscriber)
  std::array<size_t, num_stress_cogs + 1U> run_counters{};

  /// Subscriber cog message counters
  std::array<size_t, num_stress_cogs> recv_counters{};

  /// Dropped message counters
  std::array<size_t, num_stress_cogs> drop_counters{};

  /// Next sequence number to send for each timer cog
  std::array<int32_t, num_stress_cogs> send_exec_counts{};

  /// Next sequence number expected to be received from each cog
  std::array<int32_t, num_stress_cogs> recv_exec_counts{};
};

} // namespace clockwork::tests
