// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/dsl/cog/ten_nanosecond_type.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/time/sync_time.hh"

#include <ctime>
#include <sys/resource.h>

namespace clockwork
{

/// Raw timing data captured at one boundary of generated cog execution.
struct ExecuteCogTimingSnapshot
{
  jewels::time::SyncTime wall_time{};
  timespec thread_cpu_time{};
  rusage thread_usage{};
};

/// Durations spent only in a generated cog's execute body.
struct ExecuteCogMetrics
{
  TenNanoseconds wall_duration{};
  TenNanoseconds thread_cpu_duration{};
  TenNanoseconds thread_user_duration{};
  TenNanoseconds thread_system_duration{};
};

/// Capture timing values at a generated cog execution boundary.
jewels::BinaryOutcome
capture_execute_cog_timing(jewels::Out<ExecuteCogTimingSnapshot> snapshot_out, jewels::time::SyncTime wall_time);

/// Calculate bounded recorded durations from two execution timing snapshots.
void calculate_execute_cog_metrics(
  jewels::Out<ExecuteCogMetrics> metrics_out,
  const ExecuteCogTimingSnapshot& start,
  const ExecuteCogTimingSnapshot& end);

} // namespace clockwork
