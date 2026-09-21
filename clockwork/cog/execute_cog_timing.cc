// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/execute_cog_timing.hh"

#include "jewels/time/conversions.hh"

#include <chrono>
#include <compare>
#include <cstdint>
#include <limits>
#include <sys/resource.h>

namespace clockwork
{
namespace
{

[[nodiscard]] TenNanoseconds to_recorded_duration(std::chrono::nanoseconds duration)
{
  if (duration.count() <= 0)
  {
    return TenNanoseconds{};
  }

  constexpr auto max_duration =
    std::chrono::nanoseconds{static_cast<int64_t>(std::numeric_limits<uint32_t>::max()) * 10};
  if (duration >= max_duration)
  {
    return TenNanoseconds{std::numeric_limits<uint32_t>::max()};
  }
  return std::chrono::duration_cast<TenNanoseconds>(duration);
}

[[nodiscard]] std::chrono::nanoseconds subtract_timespec(const timespec& start, const timespec& end)
{
  const auto seconds = static_cast<int64_t>(end.tv_sec) - static_cast<int64_t>(start.tv_sec);
  const auto nanoseconds = static_cast<int64_t>(end.tv_nsec) - static_cast<int64_t>(start.tv_nsec);
  return std::chrono::seconds{seconds} + std::chrono::nanoseconds{nanoseconds};
}

[[nodiscard]] std::chrono::nanoseconds subtract_timeval(const timeval& start, const timeval& end)
{
  const auto seconds = static_cast<int64_t>(end.tv_sec) - static_cast<int64_t>(start.tv_sec);
  const auto microseconds = static_cast<int64_t>(end.tv_usec) - static_cast<int64_t>(start.tv_usec);
  return std::chrono::seconds{seconds} + std::chrono::microseconds{microseconds};
}

} // namespace

jewels::BinaryOutcome
capture_execute_cog_timing(jewels::Out<ExecuteCogTimingSnapshot> snapshot_out, jewels::time::SyncTime wall_time)
{
  snapshot_out->wall_time = wall_time;
  if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &snapshot_out->thread_cpu_time) != 0)
  {
    return jewels::failure;
  }
  if (getrusage(RUSAGE_THREAD, &snapshot_out->thread_usage) != 0)
  {
    return jewels::failure;
  }
  return jewels::success;
}

void calculate_execute_cog_metrics(
  jewels::Out<ExecuteCogMetrics> metrics_out,
  const ExecuteCogTimingSnapshot& start,
  const ExecuteCogTimingSnapshot& end)
{
  metrics_out->wall_duration = to_recorded_duration(
    std::chrono::nanoseconds{jewels::time::get_ns(end.wall_time) - jewels::time::get_ns(start.wall_time)});
  metrics_out->thread_cpu_duration =
    to_recorded_duration(subtract_timespec(start.thread_cpu_time, end.thread_cpu_time));
  metrics_out->thread_user_duration =
    to_recorded_duration(subtract_timeval(start.thread_usage.ru_utime, end.thread_usage.ru_utime));
  metrics_out->thread_system_duration =
    to_recorded_duration(subtract_timeval(start.thread_usage.ru_stime, end.thread_usage.ru_stime));
}

} // namespace clockwork
