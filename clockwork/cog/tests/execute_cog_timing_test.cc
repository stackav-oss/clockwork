// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/execute_cog_timing.hh"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

namespace clockwork
{
namespace
{

TEST_CASE("Generated cog timing records each body-only duration")
{
  ExecuteCogTimingSnapshot start{};
  start.wall_time = jewels::time::SyncTime{std::chrono::nanoseconds{1'000}};
  start.thread_cpu_time = timespec{.tv_sec = 4, .tv_nsec = 900};
  start.thread_usage.ru_utime = timeval{.tv_sec = 2, .tv_usec = 900'000};
  start.thread_usage.ru_stime = timeval{.tv_sec = 1, .tv_usec = 100'000};

  ExecuteCogTimingSnapshot end{};
  end.wall_time = jewels::time::SyncTime{std::chrono::nanoseconds{2'250}};
  end.thread_cpu_time = timespec{.tv_sec = 5, .tv_nsec = 2'100};
  end.thread_usage.ru_utime = timeval{.tv_sec = 3, .tv_usec = 100'300};
  end.thread_usage.ru_stime = timeval{.tv_sec = 1, .tv_usec = 100'700};
  ExecuteCogMetrics metrics;

  calculate_execute_cog_metrics(jewels::Out{metrics}, start, end);

  CHECK(metrics.wall_duration == TenNanoseconds{125});
  CHECK(metrics.thread_cpu_duration == TenNanoseconds{100'000'120});
  CHECK(metrics.thread_user_duration == TenNanoseconds{20'030'000});
  CHECK(metrics.thread_system_duration == TenNanoseconds{70'000});
}

} // namespace
} // namespace clockwork
