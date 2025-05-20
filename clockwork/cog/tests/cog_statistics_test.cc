// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/cog_statistics.hh"
#include "clockwork/dsl/cog/ten_nanosecond_type.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/time/conversions.hh"
#include "jewels/time/sync_time.hh"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <chrono>
#include <cstdint>
#include <limits>
#include <memory_resource>
#include <optional>
#include <ratio>

namespace clockwork
{
TEST_CASE("MinMaxTests")
{
  MinMaxMean<int> min_max;
  SECTION("Uninitialized")
  {
    CHECK(!min_max.mean().has_value());
    CHECK(!min_max.max().has_value());
    CHECK(!min_max.min().has_value());
  }
  SECTION("int values")
  {
    min_max.update(4).update(7).update(2); // NOLINT(cert-err33-c) False positive

    const auto& max_value = min_max.max();
    REQUIRE(max_value.has_value());
    CHECK(*max_value == 7);

    const auto& min_value = min_max.min();
    REQUIRE(min_value.has_value());
    CHECK(*min_value == 2);

    const auto& mean_value = min_max.mean();
    REQUIRE(mean_value.has_value());
    CHECK_THAT(*mean_value, Catch::Matchers::WithinRel(static_cast<double>(4 + 7 + 2) / 3));

    min_max.clear();

    CHECK(!min_max.mean().has_value());
    CHECK(!min_max.max().has_value());
    CHECK(!min_max.min().has_value());
  }

  SECTION("With floating point")
  {
    const double fp0 = 1.1;
    const double fp1 = 5.45;
    const double fp2 = 2435.54;
    const double fp3 = -.3235;

    MinMaxMean<double> floating_point_min_max;
    floating_point_min_max.update(fp0).update(fp1).update(fp2).update(fp3); // NOLINT(cert-err33-c) False positive

    const auto& fp_max = floating_point_min_max.max();
    REQUIRE(fp_max.has_value());
    CHECK_THAT(*fp_max, Catch::Matchers::WithinRel(fp2));

    const auto& fp_min = floating_point_min_max.min();
    REQUIRE(fp_min.has_value());
    CHECK_THAT(*fp_min, Catch::Matchers::WithinRel(fp3));

    const auto& fp_mean = floating_point_min_max.mean();
    REQUIRE(fp_mean.has_value());
    CHECK_THAT(*fp_mean, Catch::Matchers::WithinRel((fp0 + fp1 + fp2 + fp3) / 4));
  }

  SECTION("With TenNanoseconds specialization")
  {
    // Test the template specialization for TenNanoseconds::mean()
    MinMaxMean<TenNanoseconds> ten_ns_min_max;

    // Add some values
    const TenNanoseconds val1(100); // 1000ns
    const TenNanoseconds val2(200); // 2000ns
    const TenNanoseconds val3(300); // 3000ns

    ten_ns_min_max.update(val3).update(val2).update(val1); // NOLINT(cert-err33-c) False positive

    // Check min/max
    const auto& ten_ns_max = ten_ns_min_max.max();
    REQUIRE(ten_ns_max.has_value());
    CHECK(*ten_ns_max == val3);

    const auto& ten_ns_min = ten_ns_min_max.min();
    REQUIRE(ten_ns_min.has_value());
    CHECK(ten_ns_min->count() == val1.count());

    // Most importantly, check mean calculation which uses count()
    const auto& ten_ns_mean = ten_ns_min_max.mean();
    REQUIRE(ten_ns_mean.has_value());

    // The mean should be (100 + 200 + 300) / 3 = 200
    CHECK_THAT(*ten_ns_mean, Catch::Matchers::WithinRel(200.0));

    // Clear and verify reset
    ten_ns_min_max.clear();
    CHECK(!ten_ns_min_max.mean().has_value());
  }
}

struct TestExecutionTimes
{
  jewels::time::SyncTime ready_time;
  jewels::time::SyncTime execution_first_attempt_time;
  jewels::time::SyncTime execution_start_time;
  jewels::time::SyncTime execution_complete_time;
};

TEST_CASE("CogMetricsTest")
{
  using namespace std::chrono_literals;
  const auto memory_resource = jewels::memory::MemoryResource(std::pmr::new_delete_resource());

  constexpr auto start_time1 = jewels::time::SyncTime(3000ms);
  TestExecutionTimes run1;
  run1.ready_time = start_time1;
  run1.execution_first_attempt_time = start_time1 + 10ms;
  run1.execution_start_time = run1.execution_first_attempt_time + 30ms;
  run1.execution_complete_time = run1.execution_start_time + 123ms;

  constexpr auto start_time2 = jewels::time::SyncTime(6000ms);
  TestExecutionTimes run2;
  run2.ready_time = start_time2;
  run2.execution_first_attempt_time = start_time2 + 11ns;
  run2.execution_start_time = run2.execution_first_attempt_time + 36ms;
  run2.execution_complete_time = run2.execution_start_time + 234ms;

  const TenNanoseconds run1_execution_duration = std::chrono::duration_cast<TenNanoseconds>(std::chrono::nanoseconds(
    jewels::time::get_ns(run1.execution_complete_time) - jewels::time::get_ns(run1.execution_start_time)));

  const TenNanoseconds run1_first_ready_to_execution = std::chrono::duration_cast<TenNanoseconds>(
    std::chrono::nanoseconds(jewels::time::get_ns(run1.execution_start_time) - jewels::time::get_ns(run1.ready_time)));

  const TenNanoseconds run1_first_attempt_to_execution =
    std::chrono::duration_cast<TenNanoseconds>(std::chrono::nanoseconds(
      jewels::time::get_ns(run1.execution_start_time) - jewels::time::get_ns(run1.execution_first_attempt_time)));

  const TenNanoseconds run2_execution_duration = std::chrono::duration_cast<TenNanoseconds>(std::chrono::nanoseconds(
    jewels::time::get_ns(run2.execution_complete_time) - jewels::time::get_ns(run2.execution_start_time)));

  SECTION("Nominal Case")
  {
    CogMetrics test_metrics{memory_resource};
    test_metrics.cog_ready(run1.ready_time);
    CHECK(test_metrics.execution_attempted(run1.execution_first_attempt_time).has_value());
    CHECK(test_metrics.execution_started(run1.execution_start_time).has_value());
    CHECK(test_metrics.execution_completed(run1.execution_complete_time).has_value());

    CHECK(test_metrics.event_metrics().size() == 1);

    auto telemetry_metrics = test_metrics.telemetry_metrics();
    CHECK(telemetry_metrics.num_executions == 1);

    const auto& num_requeues_max = telemetry_metrics.num_requeues_before_execution.max();
    REQUIRE(num_requeues_max.has_value());
    CHECK(*num_requeues_max == 0);

    const auto& exec_duration_max = telemetry_metrics.execution_duration.max();
    REQUIRE(exec_duration_max.has_value());
    CHECK(*exec_duration_max == run1_execution_duration);

    const auto& latency_attempt_max = telemetry_metrics.latency_first_attempt_to_execution.max();
    REQUIRE(latency_attempt_max.has_value());
    CHECK(*latency_attempt_max == run1_first_attempt_to_execution);

    const auto& latency_ready_max = telemetry_metrics.latency_first_ready_to_execution.max();
    REQUIRE(latency_ready_max.has_value());
    CHECK(*latency_ready_max == run1_first_ready_to_execution);

    // Only one execution so there is no valid execution period.
    CHECK(telemetry_metrics.execution_period.max() == std::nullopt);
    // Bad state transitions
    SECTION("Bad state transitions")
    {
      CHECK(!test_metrics.execution_completed(run2.execution_complete_time).has_value());
      CHECK(!test_metrics.execution_attempted(run2.execution_first_attempt_time).has_value());
      CHECK(!test_metrics.execution_started(run2.execution_start_time).has_value());

      CHECK(test_metrics.event_metrics().size() == 1);
    }
    // Going to cog ready effectively gets us back into a valid state.
    test_metrics.cog_ready(run2.ready_time);

    CHECK(test_metrics.execution_attempted(run2.execution_first_attempt_time).has_value());
    CHECK(test_metrics.execution_attempted(run2.execution_first_attempt_time).has_value());
    CHECK(test_metrics.execution_started(run2.execution_start_time).has_value());
    CHECK(test_metrics.execution_completed(run2.execution_complete_time).has_value());

    auto event_metrics = test_metrics.event_metrics().back();
    CHECK(test_metrics.event_metrics().size() == 2);

    // Use proper chrono duration comparison
    CHECK(
      event_metrics.execution_duration ==
      std::chrono::duration_cast<TenNanoseconds>(std::chrono::nanoseconds(
        jewels::time::get_ns(run2.execution_complete_time) - jewels::time::get_ns(run2.execution_start_time))));

    CHECK(event_metrics.num_requeues_before_execution == 1);

    const auto& exec_duration_mean = test_metrics.telemetry_metrics().execution_duration.mean();
    REQUIRE(exec_duration_mean.has_value());

    // Convert to double for comparison
    const double expected_mean = (run1_execution_duration.count() + run2_execution_duration.count()) / 2.0;
    CHECK_THAT(*exec_duration_mean, Catch::Matchers::WithinRel(expected_mean));

    const auto& exec_period_max = test_metrics.telemetry_metrics().execution_period.max();
    REQUIRE(exec_period_max.has_value());

    // Compare as TenNanoseconds
    const TenNanoseconds expected_period = std::chrono::duration_cast<TenNanoseconds>(std::chrono::nanoseconds(
      jewels::time::get_ns(run2.execution_start_time) - jewels::time::get_ns(run1.execution_start_time)));
    CHECK(*exec_period_max == expected_period);
  }

  SECTION("Duration overflow handling")
  {
    using namespace std::chrono_literals;
    CogMetrics test_metrics{memory_resource};

    // Create times with a gap larger than maximum representable duration (~42.9s)
    // Max value for TenNanoseconds is uint32_max * 10ns ≈ 42.9s
    constexpr auto start_time = jewels::time::SyncTime(0s);
    constexpr auto ready_time = start_time;
    constexpr auto attempt_time = start_time + 1ms;
    constexpr auto execution_time = start_time + 2ms;

    // Set completion time far in the future to guarantee overflow
    // This is well beyond the ~42.9s maximum representable by a TenNanoseconds
    constexpr auto completion_time = start_time + 60s;

    // Execute the regular workflow
    test_metrics.cog_ready(ready_time);
    CHECK(test_metrics.execution_attempted(attempt_time).has_value());
    CHECK(test_metrics.execution_started(execution_time).has_value());
    CHECK(test_metrics.execution_completed(completion_time).has_value());

    // Verify metrics
    CHECK(test_metrics.event_metrics().size() == 1);

    // Check that execution_duration is capped at maximum value rather than wrapping
    const auto event_metrics = test_metrics.event_metrics().front();
    CHECK(event_metrics.execution_duration.count() == std::numeric_limits<uint32_t>::max());

    // Also verify the telemetry metrics
    const auto& telemetry = test_metrics.telemetry_metrics();
    const auto& exec_duration_max = telemetry.execution_duration.max();
    REQUIRE(exec_duration_max.has_value());

    // The max value in telemetry should be the maximum representable by TenNanoseconds
    CHECK(*exec_duration_max == TenNanoseconds(std::numeric_limits<uint32_t>::max()));
  }

  SECTION("Event metrics overflow handling")
  {
    using namespace std::chrono_literals;
    CogMetrics test_metrics{memory_resource};

    constexpr auto start_time = jewels::time::SyncTime(0s);
    constexpr size_t max_events = 10; // This should match event_metrics_batch_size

    // Fill the event metrics to capacity
    for (size_t i = 0; i < max_events; ++i)
    {
      auto event_time = start_time + std::chrono::milliseconds(i * 100);

      // Complete execution sequence
      test_metrics.cog_ready(event_time);
      CHECK(test_metrics.execution_attempted(event_time + 1ms).has_value());
      CHECK(test_metrics.execution_started(event_time + 2ms).has_value());
      CHECK(test_metrics.execution_completed(event_time + 10ms).has_value());
    }

    // Verify we've reached capacity
    CHECK(test_metrics.is_event_metrics_batch_full());
    CHECK(test_metrics.current_event_metrics_batch_size() == max_events);

    // Attempt to add one more event - should fail
    auto overflow_time = start_time + 1100ms;
    test_metrics.cog_ready(overflow_time);
    CHECK(test_metrics.execution_attempted(overflow_time + 1ms).has_value());
    CHECK(test_metrics.execution_started(overflow_time + 2ms).has_value());

    // This should fail with an error
    auto result = test_metrics.execution_completed(overflow_time + 10ms);
    CHECK(!result.has_value());

    // Verify that no additional event was added
    CHECK(test_metrics.event_metrics().size() == max_events);
    CHECK(test_metrics.current_event_metrics_batch_size() == max_events);

    // Now test that reset_metrics() allows adding events again
    test_metrics.reset_metrics();

    // Verify metrics have been cleared
    CHECK(!test_metrics.is_event_metrics_batch_full());
    CHECK(test_metrics.current_event_metrics_batch_size() == 0);

    // Attempt to add a new event after reset - should succeed
    auto new_time = start_time + 2000ms;
    test_metrics.cog_ready(new_time);
    CHECK(test_metrics.execution_attempted(new_time + 1ms).has_value());
    CHECK(test_metrics.execution_started(new_time + 2ms).has_value());
    auto new_result = test_metrics.execution_completed(new_time + 10ms);
    CHECK(new_result.has_value());

    // Verify the event was added successfully
    CHECK(test_metrics.current_event_metrics_batch_size() == 1);
  }
}
} // namespace clockwork
