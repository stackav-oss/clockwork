// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/include_common.hh"
#include "clockwork/dsl/ir/tests/support/report_group_defs.hh"
#include "clockwork/dsl/ir/tests/support/report_group_defs_dial.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/time/sync_time.hh"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace clockwork::dsl::tests::support
{

TEST_CASE("BatchedCog SignalApi has expected setter methods")
{
  BatchedCogDialSignalApi signal_api{};

  SECTION("set_basic_batched exists and takes uint64_t")
  {
    signal_api.set_basic_batched(42U);
  }

  SECTION("accumulate_multi_pre_agg exists and takes int32_t")
  {
    signal_api.accumulate_multi_pre_agg(100);
  }

  SECTION("accumulate_mean_pre_agg exists and takes uint16_t")
  {
    signal_api.accumulate_mean_pre_agg(static_cast<uint16_t>(50));
  }

  SECTION("accumulate_with_metadata exists and takes float + SyncTime")
  {
    signal_api.accumulate_with_metadata(3.14f, jewels::time::SyncTime{});
  }

  SECTION("accumulate_metadata_stripped exists and takes uint32_t + SyncTime")
  {
    signal_api.accumulate_metadata_stripped(999U, jewels::time::SyncTime{});
  }
}

TEST_CASE("BatchedCog SignalApi has expected getter methods")
{
  BatchedCogDialSignalApi signal_api{};

  SECTION("get_basic_batched exists with Out<uint64_t> parameter")
  {
    uint64_t value = 0;
    auto result = signal_api.get_basic_batched(jewels::Out(value));
    static_assert(std::is_same_v<decltype(result), jewels::BinaryOutcome>);
    // Should fail since no value was set
    REQUIRE(result.fails());
  }

  SECTION("get_multi_pre_agg_min exists with Out<int32_t> parameter")
  {
    int32_t value = 0;
    auto result = signal_api.get_multi_pre_agg_min(jewels::Out(value));
    static_assert(std::is_same_v<decltype(result), jewels::BinaryOutcome>);
    REQUIRE(result.fails());
  }

  SECTION("get_multi_pre_agg_max exists with Out<int32_t> parameter")
  {
    int32_t value = 0;
    auto result = signal_api.get_multi_pre_agg_max(jewels::Out(value));
    static_assert(std::is_same_v<decltype(result), jewels::BinaryOutcome>);
    REQUIRE(result.fails());
  }

  // Test getters for MEAN expanded to SUM and COUNT
  SECTION("get_mean_pre_agg_sum exists with Out<uint16_t> parameter")
  {
    uint16_t value = 0;
    auto result = signal_api.get_mean_pre_agg_sum(jewels::Out(value));
    static_assert(std::is_same_v<decltype(result), jewels::BinaryOutcome>);
    REQUIRE(result.fails());
  }

  SECTION("get_mean_pre_agg_count exists with Out<uint16_t> parameter")
  {
    uint16_t value = 0;
    auto result = signal_api.get_mean_pre_agg_count(jewels::Out(value));
    static_assert(std::is_same_v<decltype(result), jewels::BinaryOutcome>);
    REQUIRE(result.fails());
  }

  SECTION("get_with_metadata exists with Out<float> and OptionalOut<SyncTime>")
  {
    float value = 0.0f;
    jewels::time::SyncTime metadata{};
    auto result = signal_api.get_with_metadata(jewels::Out(value), jewels::OptionalOut(metadata));
    static_assert(std::is_same_v<decltype(result), jewels::BinaryOutcome>);
    REQUIRE(result.fails());
  }

  SECTION("get_metadata_stripped_sum exists with Out<uint32_t> parameter (no metadata)")
  {
    uint32_t value = 0;
    auto result = signal_api.get_metadata_stripped_sum(jewels::Out(value));
    static_assert(std::is_same_v<decltype(result), jewels::BinaryOutcome>);
    REQUIRE(result.fails());
  }

  SECTION("get_metadata_stripped_count exists with Out<uint32_t> parameter (no metadata)")
  {
    uint32_t value = 0;
    auto result = signal_api.get_metadata_stripped_count(jewels::Out(value));
    static_assert(std::is_same_v<decltype(result), jewels::BinaryOutcome>);
    REQUIRE(result.fails());
  }
}

TEST_CASE("BatchedCog SignalApi set/get round-trip")
{
  BatchedCogDialSignalApi signal_api{};

  SECTION("set then get for basic signal")
  {
    signal_api.set_basic_batched(12345U);

    uint64_t retrieved_value = 0;
    auto result = signal_api.get_basic_batched(jewels::Out(retrieved_value));
    REQUIRE(result.ok());
    REQUIRE(retrieved_value == 12345U);
  }

  SECTION("accumulate then get for signal with min/max aggregation")
  {
    signal_api.accumulate_multi_pre_agg(100);
    signal_api.accumulate_multi_pre_agg(50);
    signal_api.accumulate_multi_pre_agg(200);

    int32_t min_value = 0;
    auto min_result = signal_api.get_multi_pre_agg_min(jewels::Out(min_value));
    REQUIRE(min_result.ok());
    REQUIRE(min_value == 50);

    int32_t max_value = 0;
    auto max_result = signal_api.get_multi_pre_agg_max(jewels::Out(max_value));
    REQUIRE(max_result.ok());
    REQUIRE(max_value == 200);
  }

  SECTION("accumulate then get for signal with mean (sum/count) aggregation")
  {
    signal_api.accumulate_mean_pre_agg(static_cast<uint16_t>(10));
    signal_api.accumulate_mean_pre_agg(static_cast<uint16_t>(20));
    signal_api.accumulate_mean_pre_agg(static_cast<uint16_t>(30));

    uint16_t sum_value = 0;
    auto sum_result = signal_api.get_mean_pre_agg_sum(jewels::Out(sum_value));
    REQUIRE(sum_result.ok());
    REQUIRE(sum_value == 60); // 10 + 20 + 30

    uint16_t count_value = 0;
    auto count_result = signal_api.get_mean_pre_agg_count(jewels::Out(count_value));
    REQUIRE(count_result.ok());
    REQUIRE(count_value == 3);
  }

  SECTION("accumulate then get with metadata preserved")
  {
    const auto metadata1 = jewels::time::SyncTime{std::chrono::nanoseconds{1000}};
    const auto metadata2 = jewels::time::SyncTime{std::chrono::nanoseconds{2000}};

    // First value should be stored (it's min)
    signal_api.accumulate_with_metadata(5.0f, metadata1);
    // Second value is smaller, so it becomes the new min
    signal_api.accumulate_with_metadata(3.0f, metadata2);

    float retrieved_value = 0.0f;
    jewels::time::SyncTime retrieved_metadata{};
    auto result = signal_api.get_with_metadata(jewels::Out(retrieved_value), jewels::OptionalOut(retrieved_metadata));
    REQUIRE(result.ok());
    REQUIRE(retrieved_value == 3.0f);
    REQUIRE(retrieved_metadata == metadata2);
  }

  SECTION("accumulate then get with metadata stripped")
  {
    const auto metadata = jewels::time::SyncTime{std::chrono::nanoseconds{1000}};

    signal_api.accumulate_metadata_stripped(100U, metadata);
    signal_api.accumulate_metadata_stripped(200U, metadata);

    uint32_t sum_value = 0;
    auto sum_result = signal_api.get_metadata_stripped_sum(jewels::Out(sum_value));
    REQUIRE(sum_result.ok());
    REQUIRE(sum_value == 300U);

    uint32_t count_value = 0;
    auto count_result = signal_api.get_metadata_stripped_count(jewels::Out(count_value));
    REQUIRE(count_result.ok());
    REQUIRE(count_value == 2);
  }
}

TEST_CASE("BatchedCog Dial get_signals returns SignalApi reference")
{
  // Test that Dial has get_signals method returning SignalApi&
  static_assert(std::is_same_v<decltype(std::declval<BatchedCogDial&>().get_signals()), BatchedCogDialSignalApi&>);
}

TEST_CASE("ComprehensiveCog SignalApi has post-aggregated signal methods")
{
  // ComprehensiveCog uses post-aggregated signals, so SignalApi should have post-agg methods
  static_assert(std::is_default_constructible_v<ComprehensiveCogDialSignalApi>);

  ComprehensiveCogDialSignalApi signal_api{};

  // Test that the execution count starts at 0
  REQUIRE(signal_api.get_execution_count_test_group() == 0);
}

TEST_CASE("BatchedCog SignalApi has per-group batch_size constant")
{
  // batch_size_<group> should be the max_observations from the policy (25 for batched_group)
  static_assert(BatchedCogDialSignalApi::batch_size_batched_group == 25);
}

TEST_CASE("BatchedCog SignalApi per-group batch management methods")
{
  BatchedCogDialSignalApi signal_api{};
  using Policy = BatchedCogPolicy;

  SECTION("get_batch_count_<group> returns 0 initially")
  {
    // Initially no executions have completed
    REQUIRE(signal_api.get_batch_count_batched_group() == 0);
  }
  SECTION("end_of_execution_<group> advances to next batch slot")
  {
    REQUIRE(signal_api.get_batch_count_batched_group() == 0);
    Policy::end_of_execution_batched_group(signal_api, jewels::time::SyncTime{});
    REQUIRE(signal_api.get_batch_count_batched_group() == 1);

    Policy::end_of_execution_batched_group(signal_api, jewels::time::SyncTime{});
    REQUIRE(signal_api.get_batch_count_batched_group() == 2);

    Policy::end_of_execution_batched_group(signal_api, jewels::time::SyncTime{});
    REQUIRE(signal_api.get_batch_count_batched_group() == 3);
  }

  SECTION("end_of_execution_<group> stops at batch_size")
  {
    // Advance to the maximum
    for (size_t i = 0; i < BatchedCogDialSignalApi::batch_size_batched_group + 5; ++i)
    {
      Policy::end_of_execution_batched_group(signal_api, jewels::time::SyncTime{});
    }
    // Should not exceed batch_size
    REQUIRE(signal_api.get_batch_count_batched_group() == BatchedCogDialSignalApi::batch_size_batched_group);
  }

  SECTION("reset_batch_<group> resets batch count to 0")
  {
    Policy::end_of_execution_batched_group(signal_api, jewels::time::SyncTime{});
    Policy::end_of_execution_batched_group(signal_api, jewels::time::SyncTime{});
    REQUIRE(signal_api.get_batch_count_batched_group() == 2);

    Policy::reset_batch_batched_group(signal_api);
    REQUIRE(signal_api.get_batch_count_batched_group() == 0);
  }

  SECTION("get_batch_size_<group> returns correct batch size")
  {
    REQUIRE(signal_api.get_batch_size_batched_group() == 25);
  }
}

TEST_CASE("BatchedCog SignalApi stores data per batch slot")
{
  BatchedCogDialSignalApi signal_api{};
  using Policy = BatchedCogPolicy;

  SECTION("data is stored in separate batch slots")
  {
    // First execution: set value 100
    signal_api.set_basic_batched(100U);
    Policy::end_of_execution_batched_group(signal_api, jewels::time::SyncTime{});

    // Second execution: set value 200
    signal_api.set_basic_batched(200U);
    Policy::end_of_execution_batched_group(signal_api, jewels::time::SyncTime{});

    // Third execution: set value 300
    signal_api.set_basic_batched(300U);

    // Getter should return value for current batch slot (300)
    uint64_t value = 0;
    auto result = signal_api.get_basic_batched(jewels::Out(value));
    REQUIRE(result.ok());
    REQUIRE(value == 300U);
  }

  SECTION("reset_batch_<group> clears all aggregators")
  {
    // Set some values
    signal_api.set_basic_batched(100U);
    Policy::end_of_execution_batched_group(signal_api, jewels::time::SyncTime{});
    signal_api.set_basic_batched(200U);

    // Verify we have data
    uint64_t value = 0;
    auto result = signal_api.get_basic_batched(jewels::Out(value));
    REQUIRE(result.ok());

    // Reset the batch
    Policy::reset_batch_batched_group(signal_api);

    // Now getter should fail (no value set after reset)
    result = signal_api.get_basic_batched(jewels::Out(value));
    REQUIRE(result.fails());
  }

  SECTION("aggregators work independently per batch slot")
  {
    // First execution: accumulate values 100, 50, 200 -> min=50, max=200
    signal_api.accumulate_multi_pre_agg(100);
    signal_api.accumulate_multi_pre_agg(50);
    signal_api.accumulate_multi_pre_agg(200);
    Policy::end_of_execution_batched_group(signal_api, jewels::time::SyncTime{});

    // Second execution: accumulate values 10, 5, 20 -> min=5, max=20
    signal_api.accumulate_multi_pre_agg(10);
    signal_api.accumulate_multi_pre_agg(5);
    signal_api.accumulate_multi_pre_agg(20);

    // Getter should return values for current batch slot (second execution)
    int32_t min_value = 0;
    auto min_result = signal_api.get_multi_pre_agg_min(jewels::Out(min_value));
    REQUIRE(min_result.ok());
    REQUIRE(min_value == 5);

    int32_t max_value = 0;
    auto max_result = signal_api.get_multi_pre_agg_max(jewels::Out(max_value));
    REQUIRE(max_result.ok());
    REQUIRE(max_value == 20);
  }

  SECTION("sum and count aggregators work per batch slot")
  {
    // First execution: accumulate 10, 20, 30 -> sum=60, count=3
    signal_api.accumulate_mean_pre_agg(static_cast<uint16_t>(10));
    signal_api.accumulate_mean_pre_agg(static_cast<uint16_t>(20));
    signal_api.accumulate_mean_pre_agg(static_cast<uint16_t>(30));
    Policy::end_of_execution_batched_group(signal_api, jewels::time::SyncTime{});

    // Second execution: accumulate 1, 2 -> sum=3, count=2
    signal_api.accumulate_mean_pre_agg(static_cast<uint16_t>(1));
    signal_api.accumulate_mean_pre_agg(static_cast<uint16_t>(2));

    // Getter should return values for current batch slot
    uint16_t sum_value = 0;
    auto sum_result = signal_api.get_mean_pre_agg_sum(jewels::Out(sum_value));
    REQUIRE(sum_result.ok());
    REQUIRE(sum_value == 3);

    uint16_t count_value = 0;
    auto count_result = signal_api.get_mean_pre_agg_count(jewels::Out(count_value));
    REQUIRE(count_result.ok());
    REQUIRE(count_value == 2);
  }
}

// =============================================================================
// Post-aggregated Signal API Tests (ComprehensiveCog)
// =============================================================================

TEST_CASE("ComprehensiveCog SignalApi set/accumulate methods exist")
{
  ComprehensiveCogDialSignalApi signal_api{};

  SECTION("set_basic_signal exists and takes uint64_t")
  {
    signal_api.set_basic_signal(42U);
  }

  SECTION("accumulate_combined_signal exists and takes Duration")
  {
    signal_api.accumulate_combined_signal(std::chrono::nanoseconds{100});
  }

  SECTION("accumulate_mean_signal exists and takes uint32_t")
  {
    signal_api.accumulate_mean_signal(50U);
  }

  SECTION("accumulate_metadata_preserved exists and takes int64_t + SyncTime")
  {
    signal_api.accumulate_metadata_preserved(100, jewels::time::SyncTime{});
  }
}

TEST_CASE("ComprehensiveCog SignalApi getter methods exist")
{
  ComprehensiveCogDialSignalApi signal_api{};

  SECTION("get_basic_signal_value_min exists")
  {
    uint64_t value = 0;
    auto result = signal_api.get_basic_signal_value_min(jewels::Out(value));
    static_assert(std::is_same_v<decltype(result), jewels::BinaryOutcome>);
    // Should fail since no value was set and no execution completed
    REQUIRE(result.fails());
  }

  SECTION("get_combined_signal_min_min exists")
  {
    std::chrono::nanoseconds value{0};
    auto result = signal_api.get_combined_signal_min_min(jewels::Out(value));
    REQUIRE(result.fails());
  }

  SECTION("get_metadata_preserved with metadata parameter exists")
  {
    int64_t value = 0;
    jewels::time::SyncTime metadata{};
    auto result = signal_api.get_metadata_preserved(jewels::Out(value), jewels::OptionalOut(metadata));
    REQUIRE(result.fails());
  }

  SECTION("get_metadata_stripped without metadata parameter exists")
  {
    uint32_t value = 0;
    auto result = signal_api.get_metadata_stripped(jewels::Out(value));
    REQUIRE(result.fails());
  }
}

TEST_CASE("ComprehensiveCog SignalApi reset/end_of_execution/get_execution_count methods")
{
  ComprehensiveCogDialSignalApi signal_api{};

  SECTION("get_execution_count returns 0 initially")
  {
    REQUIRE(signal_api.get_execution_count_test_group() == 0);
  }

  SECTION("end_of_execution increments execution count")
  {
    ComprehensiveCogPolicy::end_of_execution_test_group(signal_api, jewels::time::SyncTime{});
    REQUIRE(signal_api.get_execution_count_test_group() == 1);

    ComprehensiveCogPolicy::end_of_execution_test_group(signal_api, jewels::time::SyncTime{});
    REQUIRE(signal_api.get_execution_count_test_group() == 2);
  }

  SECTION("reset sets execution count back to 0")
  {
    ComprehensiveCogPolicy::end_of_execution_test_group(signal_api, jewels::time::SyncTime{});
    ComprehensiveCogPolicy::end_of_execution_test_group(signal_api, jewels::time::SyncTime{});
    REQUIRE(signal_api.get_execution_count_test_group() == 2);

    ComprehensiveCogPolicy::reset_test_group(signal_api);
    REQUIRE(signal_api.get_execution_count_test_group() == 0);
  }
}

TEST_CASE("ComprehensiveCog SignalApi post-aggregation workflow")
{
  ComprehensiveCogDialSignalApi signal_api{};

  SECTION("basic signal with value -> min/max post-aggregation")
  {
    // First execution: set value 100
    signal_api.set_basic_signal(100U);
    ComprehensiveCogPolicy::end_of_execution_test_group(signal_api, jewels::time::SyncTime{});

    // Second execution: set value 50
    signal_api.set_basic_signal(50U);
    ComprehensiveCogPolicy::end_of_execution_test_group(signal_api, jewels::time::SyncTime{});

    // Third execution: set value 200
    signal_api.set_basic_signal(200U);
    ComprehensiveCogPolicy::end_of_execution_test_group(signal_api, jewels::time::SyncTime{});

    REQUIRE(signal_api.get_execution_count_test_group() == 3);

    // Check min post-aggregator (should be 50)
    uint64_t min_value = 0;
    auto min_result = signal_api.get_basic_signal_value_min(jewels::Out(min_value));
    REQUIRE(min_result.ok());
    REQUIRE(min_value == 50U);

    // Check max post-aggregator (should be 200)
    uint64_t max_value = 0;
    auto max_result = signal_api.get_basic_signal_value_max(jewels::Out(max_value));
    REQUIRE(max_result.ok());
    REQUIRE(max_value == 200U);
  }

  SECTION("reset clears post-aggregators")
  {
    signal_api.set_basic_signal(100U);
    ComprehensiveCogPolicy::end_of_execution_test_group(signal_api, jewels::time::SyncTime{});

    // Verify we have data
    uint64_t value = 0;
    auto result = signal_api.get_basic_signal_value_min(jewels::Out(value));
    REQUIRE(result.ok());

    // Reset
    ComprehensiveCogPolicy::reset_test_group(signal_api);

    // Now getter should fail
    result = signal_api.get_basic_signal_value_min(jewels::Out(value));
    REQUIRE(result.fails());
  }

  SECTION("signal with pre-aggregation (min/max) and post-aggregation (min/final_value)")
  {
    // First execution: accumulate values 100, 50, 200 -> pre-agg min=50, max=200
    signal_api.accumulate_combined_signal(std::chrono::nanoseconds{100});
    signal_api.accumulate_combined_signal(std::chrono::nanoseconds{50});
    signal_api.accumulate_combined_signal(std::chrono::nanoseconds{200});
    ComprehensiveCogPolicy::end_of_execution_test_group(signal_api, jewels::time::SyncTime{});

    // Second execution: accumulate values 10, 5 -> pre-agg min=5, max=10
    signal_api.accumulate_combined_signal(std::chrono::nanoseconds{10});
    signal_api.accumulate_combined_signal(std::chrono::nanoseconds{5});
    ComprehensiveCogPolicy::end_of_execution_test_group(signal_api, jewels::time::SyncTime{});

    // min -> min: min of {50, 5} = 5
    std::chrono::nanoseconds min_min_value{0};
    auto min_min_result = signal_api.get_combined_signal_min_min(jewels::Out(min_min_value));
    REQUIRE(min_min_result.ok());
    REQUIRE(min_min_value == std::chrono::nanoseconds{5});

    // min -> final_value: final value of min pre-agg = 5
    std::chrono::nanoseconds min_final_value{0};
    auto min_final_result = signal_api.get_combined_signal_min_final_value(jewels::Out(min_final_value));
    REQUIRE(min_final_result.ok());
    REQUIRE(min_final_value == std::chrono::nanoseconds{5});

    // max -> min: min of {200, 10} = 10
    std::chrono::nanoseconds max_min_value{0};
    auto max_min_result = signal_api.get_combined_signal_max_min(jewels::Out(max_min_value));
    REQUIRE(max_min_result.ok());
    REQUIRE(max_min_value == std::chrono::nanoseconds{10});
  }

  SECTION("mean post-aggregation")
  {
    // First execution: accumulate values with sum=100
    signal_api.accumulate_mean_signal(100U);
    ComprehensiveCogPolicy::end_of_execution_test_group(signal_api, jewels::time::SyncTime{});

    // Second execution: accumulate values with sum=200
    signal_api.accumulate_mean_signal(200U);
    ComprehensiveCogPolicy::end_of_execution_test_group(signal_api, jewels::time::SyncTime{});

    // Third execution: accumulate values with sum=300
    signal_api.accumulate_mean_signal(300U);
    ComprehensiveCogPolicy::end_of_execution_test_group(signal_api, jewels::time::SyncTime{});

    // sum -> mean: mean of {100, 200, 300} = 200.0 (returned as double)
    double mean_value = 0.0;
    auto mean_result = signal_api.get_mean_signal_sum_mean(jewels::Out(mean_value));
    REQUIRE(mean_result.ok());
    REQUIRE(mean_value == Catch::Approx(200.0));

    // sum -> max: max of {100, 200, 300} = 300
    uint32_t max_value = 0;
    auto max_result = signal_api.get_mean_signal_sum_max(jewels::Out(max_value));
    REQUIRE(max_result.ok());
    REQUIRE(max_value == 300U);
  }

  SECTION("metadata preserved through post-aggregation")
  {
    const auto metadata1 = jewels::time::SyncTime{std::chrono::nanoseconds{1000}};
    const auto metadata2 = jewels::time::SyncTime{std::chrono::nanoseconds{2000}};

    // First execution: accumulate value 100 with metadata1
    signal_api.accumulate_metadata_preserved(100, metadata1);
    ComprehensiveCogPolicy::end_of_execution_test_group(signal_api, jewels::time::SyncTime{});

    // Second execution: accumulate value 50 with metadata2 (this should be the max value)
    signal_api.accumulate_metadata_preserved(50, metadata2);
    ComprehensiveCogPolicy::end_of_execution_test_group(signal_api, jewels::time::SyncTime{});

    // min -> max post-aggregation: max of {100, 50} = 100 with metadata1
    int64_t value = 0;
    jewels::time::SyncTime metadata{};
    auto result = signal_api.get_metadata_preserved(jewels::Out(value), jewels::OptionalOut(metadata));
    REQUIRE(result.ok());
    REQUIRE(value == 100);
    REQUIRE(metadata == metadata1);
  }
}

TEST_CASE("BatchedCog SignalApi window management methods")
{
  BatchedCogDialSignalApi signal_api{};

  SECTION("start_of_execution_<group> exists and takes SyncTime")
  {
    BatchedCogPolicy::start_of_execution_batched_group(signal_api, jewels::time::SyncTime{});
  }

  SECTION("should_publish_<group> returns false without start_of_execution")
  {
    // Adding observations without calling start_of_execution should still return false
    signal_api.set_basic_batched(100U);
    REQUIRE_FALSE(BatchedCogPolicy::should_publish_batched_group(signal_api));
  }

  SECTION("should_publish_<group> returns false before batch is full")
  {
    const auto start_time = jewels::time::SyncTime{std::chrono::nanoseconds{0}};
    BatchedCogPolicy::start_of_execution_batched_group(signal_api, start_time);
    signal_api.set_basic_batched(100U);
    const auto end_time = jewels::time::SyncTime{std::chrono::nanoseconds{1'000'000'000}}; // 1 second later
    BatchedCogPolicy::end_of_execution_batched_group(signal_api, end_time);

    // With only 1 observation and batch_size of 25, should not publish yet
    REQUIRE_FALSE(BatchedCogPolicy::should_publish_batched_group(signal_api));
  }

  SECTION("should_publish_<group> returns true when batch is full")
  {
    const auto start_time = jewels::time::SyncTime{std::chrono::nanoseconds{0}};
    BatchedCogPolicy::start_of_execution_batched_group(signal_api, start_time);

    // Fill up the batch (batch_size is 25)
    for (size_t i = 0; i < BatchedCogDialSignalApi::batch_size_batched_group - 1; ++i)
    {
      signal_api.set_basic_batched(static_cast<uint64_t>(i));
      const auto end_time = jewels::time::SyncTime{std::chrono::nanoseconds{(i + 1) * 1000}};
      BatchedCogPolicy::end_of_execution_batched_group(signal_api, end_time);
    }
    signal_api.set_basic_batched(999U); // Last observation
    const auto final_end_time = jewels::time::SyncTime{std::chrono::nanoseconds{1'000'000'000}};
    BatchedCogPolicy::end_of_execution_batched_group(signal_api, final_end_time);

    // Now batch is full (25 observations), should publish
    REQUIRE(BatchedCogPolicy::should_publish_batched_group(signal_api));
  }

  SECTION("should_publish_<group> returns true when max_duration is exceeded before batch is full")
  {
    const auto start_time = jewels::time::SyncTime{std::chrono::nanoseconds{0}};
    BatchedCogPolicy::start_of_execution_batched_group(signal_api, start_time);

    // Add only 1 observation, far fewer than batch_size of 25
    signal_api.set_basic_batched(100U);
    // Elapsed time of 11 seconds exceeds max_duration of 10 seconds
    const auto end_time = jewels::time::SyncTime{std::chrono::nanoseconds{11'000'000'000LL}};
    BatchedCogPolicy::end_of_execution_batched_group(signal_api, end_time);

    // max_duration exceeded, should publish even with an incomplete batch
    REQUIRE(BatchedCogPolicy::should_publish_batched_group(signal_api));
  }

  SECTION("reset_batch_<group> resets window start time")
  {
    const auto start_time = jewels::time::SyncTime{std::chrono::nanoseconds{0}};
    BatchedCogPolicy::start_of_execution_batched_group(signal_api, start_time);
    signal_api.set_basic_batched(100U);
    const auto end_time = jewels::time::SyncTime{std::chrono::nanoseconds{1'000'000'000}};
    BatchedCogPolicy::end_of_execution_batched_group(signal_api, end_time);

    // Reset the batch
    BatchedCogPolicy::reset_batch_batched_group(signal_api);

    // After reset, should_publish should return false (window not started)
    REQUIRE_FALSE(BatchedCogPolicy::should_publish_batched_group(signal_api));
  }
}

TEST_CASE("ComprehensiveCog SignalApi post-aggregated window management")
{
  ComprehensiveCogDialSignalApi signal_api{};

  SECTION("start_of_execution_<group> exists for post-aggregated groups")
  {
    ComprehensiveCogPolicy::start_of_execution_test_group(signal_api, jewels::time::SyncTime{});
  }

  SECTION("should_publish_<group> returns false without start_of_execution")
  {
    // Adding observations without calling start_of_execution should still return false
    signal_api.set_basic_signal(100U);
    const auto end_time = jewels::time::SyncTime{std::chrono::nanoseconds{1'000'000'000}};
    ComprehensiveCogPolicy::end_of_execution_test_group(signal_api, end_time);
    REQUIRE_FALSE(ComprehensiveCogPolicy::should_publish_test_group(signal_api));
  }

  SECTION("should_publish_<group> returns true when max_observations is reached")
  {
    const auto start_time = jewels::time::SyncTime{std::chrono::nanoseconds{0}};
    ComprehensiveCogPolicy::start_of_execution_test_group(signal_api, start_time);

    // max_observations is 100 for test_group
    for (size_t i = 0; i < 100; ++i)
    {
      signal_api.set_basic_signal(static_cast<uint64_t>(i));
      const auto end_time = jewels::time::SyncTime{std::chrono::nanoseconds{(i + 1) * 1000}};
      ComprehensiveCogPolicy::end_of_execution_test_group(signal_api, end_time);
    }

    REQUIRE(ComprehensiveCogPolicy::should_publish_test_group(signal_api));
  }

  SECTION("should_publish_<group> returns true when max_duration is exceeded before max_observations")
  {
    const auto start_time = jewels::time::SyncTime{std::chrono::nanoseconds{0}};
    ComprehensiveCogPolicy::start_of_execution_test_group(signal_api, start_time);

    // Add only 1 execution, far fewer than max_observations of 100
    signal_api.set_basic_signal(100U);
    // Elapsed time of 31 seconds exceeds max_duration of 30 seconds
    const auto end_time = jewels::time::SyncTime{std::chrono::nanoseconds{31'000'000'000LL}};
    ComprehensiveCogPolicy::end_of_execution_test_group(signal_api, end_time);

    // max_duration exceeded, should publish even with fewer than max_observations
    REQUIRE(ComprehensiveCogPolicy::should_publish_test_group(signal_api));
  }

  SECTION("reset_<group> resets window start time for post-aggregated groups")
  {
    const auto start_time = jewels::time::SyncTime{std::chrono::nanoseconds{0}};
    ComprehensiveCogPolicy::start_of_execution_test_group(signal_api, start_time);
    signal_api.set_basic_signal(100U);
    const auto end_time = jewels::time::SyncTime{std::chrono::nanoseconds{1'000'000'000}};
    ComprehensiveCogPolicy::end_of_execution_test_group(signal_api, end_time);

    // Reset the group
    ComprehensiveCogPolicy::reset_test_group(signal_api);

    // After reset, should_publish should return false (window not started)
    REQUIRE_FALSE(ComprehensiveCogPolicy::should_publish_test_group(signal_api));
  }
}

TEST_CASE("BatchedCog SignalApi populate_batched_group with empty data", "[populate][batched]")
{
  BatchedCogDialSignalApi signal_api{};
  Tap<Tachyon<BatchedCog_batched_group>> msg{};

  BatchedCogPolicy::populate_batched_group(signal_api, msg);

  // Should have execution_interval of 0 (no window started)
  REQUIRE(msg.get_execution_interval() == std::chrono::nanoseconds{0});

  // Should have 0 elements (no completed executions)
  REQUIRE(msg.get_signals().size() == 0);
}

TEST_CASE("BatchedCog SignalApi populate_batched_group with single execution", "[populate][batched]")
{
  BatchedCogDialSignalApi signal_api{};

  const auto start_time = jewels::time::SyncTime{std::chrono::nanoseconds{1'000'000'000}}; // 1 second
  const auto end_time = jewels::time::SyncTime{std::chrono::nanoseconds{2'500'000'000}};   // 2.5 seconds
  const auto metadata1 = jewels::time::SyncTime{std::chrono::nanoseconds{1'234'567'890}};

  // Start window and accumulate some data
  BatchedCogPolicy::start_of_execution_batched_group(signal_api, start_time);
  signal_api.set_basic_batched(42U);
  signal_api.accumulate_multi_pre_agg(100);
  signal_api.accumulate_multi_pre_agg(200);
  signal_api.accumulate_mean_pre_agg(static_cast<uint16_t>(10));
  signal_api.accumulate_mean_pre_agg(static_cast<uint16_t>(20));
  signal_api.accumulate_with_metadata(1.5f, metadata1);
  signal_api.accumulate_metadata_stripped(500U, metadata1);
  BatchedCogPolicy::end_of_execution_batched_group(signal_api, end_time);

  // Create message and populate
  Tap<Tachyon<BatchedCog_batched_group>> msg{};
  BatchedCogPolicy::populate_batched_group(signal_api, msg);

  // Verify execution_interval: end_time - start_time = 1.5 seconds
  const auto expected_interval = std::chrono::nanoseconds{1'500'000'000};
  REQUIRE(msg.get_execution_interval() == expected_interval);

  // Verify signals VarSoa has 1 element (1 end_of_execution call, so batch_count = 1)
  const auto& signals = msg.get_signals();
  REQUIRE(signals.size() == 1);

  // Verify signal values in the first element
  REQUIRE(signals[0].get_basic_batched_value() == 42U);
  REQUIRE(signals[0].get_multi_pre_agg_min() == 100);
  REQUIRE(signals[0].get_multi_pre_agg_max() == 200);
  REQUIRE(signals[0].get_mean_pre_agg_sum() == 30); // 10 + 20
  REQUIRE(signals[0].get_mean_pre_agg_count() == 2);
  REQUIRE(signals[0].get_with_metadata_min() == 1.5f);
  REQUIRE(signals[0].get_with_metadata_min_metadata() == metadata1);
  REQUIRE(signals[0].get_metadata_stripped_sum() == 500U);
  REQUIRE(signals[0].get_metadata_stripped_count() == 1);
}

TEST_CASE("BatchedCog SignalApi populate_batched_group with multiple executions", "[populate][batched]")
{
  BatchedCogDialSignalApi signal_api{};

  const auto start_time = jewels::time::SyncTime{std::chrono::nanoseconds{0}};
  BatchedCogPolicy::start_of_execution_batched_group(signal_api, start_time);

  // First execution
  signal_api.set_basic_batched(100U);
  signal_api.accumulate_multi_pre_agg(10);
  BatchedCogPolicy::end_of_execution_batched_group(
    signal_api, jewels::time::SyncTime{std::chrono::nanoseconds{1'000'000}});

  // Second execution
  signal_api.set_basic_batched(200U);
  signal_api.accumulate_multi_pre_agg(20);
  BatchedCogPolicy::end_of_execution_batched_group(
    signal_api, jewels::time::SyncTime{std::chrono::nanoseconds{2'000'000}});

  // Third execution
  signal_api.set_basic_batched(300U);
  signal_api.accumulate_multi_pre_agg(30);
  const auto final_end_time = jewels::time::SyncTime{std::chrono::nanoseconds{3'000'000}};
  BatchedCogPolicy::end_of_execution_batched_group(signal_api, final_end_time);

  // Create message and populate
  Tap<Tachyon<BatchedCog_batched_group>> msg{};
  BatchedCogPolicy::populate_batched_group(signal_api, msg);

  // Verify execution_interval: final_end_time - start_time = 3ms
  REQUIRE(msg.get_execution_interval() == std::chrono::nanoseconds{3'000'000});

  // Verify signals VarSoa has 3 elements (3 end_of_execution calls)
  const auto& signals = msg.get_signals();
  REQUIRE(signals.size() == 3);

  // Verify each element has the correct values
  REQUIRE(signals[0].get_basic_batched_value() == 100U);
  REQUIRE(signals[0].get_multi_pre_agg_min() == 10);
  REQUIRE(signals[0].get_multi_pre_agg_max() == 10);

  REQUIRE(signals[1].get_basic_batched_value() == 200U);
  REQUIRE(signals[1].get_multi_pre_agg_min() == 20);
  REQUIRE(signals[1].get_multi_pre_agg_max() == 20);

  REQUIRE(signals[2].get_basic_batched_value() == 300U);
  REQUIRE(signals[2].get_multi_pre_agg_min() == 30);
  REQUIRE(signals[2].get_multi_pre_agg_max() == 30);
}

TEST_CASE("BatchedCog SignalApi populate_batched_group with full batch", "[populate][batched]")
{
  BatchedCogDialSignalApi signal_api{};

  const auto start_time = jewels::time::SyncTime{std::chrono::nanoseconds{0}};
  BatchedCogPolicy::start_of_execution_batched_group(signal_api, start_time);

  // Fill up the entire batch (25 executions)
  for (size_t i = 0; i < BatchedCogDialSignalApi::batch_size_batched_group; ++i)
  {
    signal_api.set_basic_batched(static_cast<uint64_t>(i * 10));
    signal_api.accumulate_multi_pre_agg(static_cast<int32_t>(i));
    const auto end_time = jewels::time::SyncTime{std::chrono::nanoseconds{(i + 1) * 1000}};
    BatchedCogPolicy::end_of_execution_batched_group(signal_api, end_time);
  }

  // Create message and populate
  Tap<Tachyon<BatchedCog_batched_group>> msg{};
  BatchedCogPolicy::populate_batched_group(signal_api, msg);

  // Verify signals VarSoa has 25 elements (full batch)
  const auto& signals = msg.get_signals();
  REQUIRE(signals.size() == BatchedCogDialSignalApi::batch_size_batched_group);

  // Verify execution_interval
  REQUIRE(msg.get_execution_interval() == std::chrono::nanoseconds{25'000});

  // Verify first and last elements
  REQUIRE(signals[0].get_basic_batched_value() == 0U);
  REQUIRE(signals[0].get_multi_pre_agg_min() == 0);

  const auto last_idx = BatchedCogDialSignalApi::batch_size_batched_group - 1;
  REQUIRE(signals[last_idx].get_basic_batched_value() == last_idx * 10);
  REQUIRE(signals[last_idx].get_multi_pre_agg_min() == static_cast<int32_t>(last_idx));
}

TEST_CASE("BatchedCog SignalApi populate_batched_group with missing signals", "[populate][batched]")
{
  BatchedCogDialSignalApi signal_api{};

  const auto start_time = jewels::time::SyncTime{std::chrono::nanoseconds{0}};
  BatchedCogPolicy::start_of_execution_batched_group(signal_api, start_time);

  // First execution: only set basic_batched
  signal_api.set_basic_batched(100U);
  BatchedCogPolicy::end_of_execution_batched_group(
    signal_api, jewels::time::SyncTime{std::chrono::nanoseconds{1'000'000}});

  // Second execution: only set multi_pre_agg
  signal_api.accumulate_multi_pre_agg(50);
  BatchedCogPolicy::end_of_execution_batched_group(
    signal_api, jewels::time::SyncTime{std::chrono::nanoseconds{2'000'000}});

  // Create message and populate
  Tap<Tachyon<BatchedCog_batched_group>> msg{};
  BatchedCogPolicy::populate_batched_group(signal_api, msg);

  // Verify signals VarSoa has 2 elements (2 end_of_execution calls)
  const auto& signals = msg.get_signals();
  REQUIRE(signals.size() == 2);

  // First element should have basic_batched but not multi_pre_agg (optional fields are 0-initialized)
  REQUIRE(signals[0].get_basic_batched_value() == 100U);

  // Second element should have multi_pre_agg but not basic_batched
  REQUIRE(signals[1].get_multi_pre_agg_min() == 50);
  REQUIRE(signals[1].get_multi_pre_agg_max() == 50);
}

TEST_CASE("ComprehensiveCog SignalApi populate_test_group with empty data", "[populate][post-aggregated]")
{
  ComprehensiveCogDialSignalApi signal_api{};
  Tap<Tachyon<ComprehensiveCog_test_group>> msg{};

  // Populate with no data accumulated
  ComprehensiveCogPolicy::populate_test_group(signal_api, msg);

  // Should have execution_count of 0
  REQUIRE(msg.get_execution_count() == 0);

  // Should have execution_interval of 0 (no window started)
  REQUIRE(msg.get_execution_interval() == std::chrono::nanoseconds{0});
}

TEST_CASE("ComprehensiveCog SignalApi populate_test_group with single execution", "[populate][post-aggregated]")
{
  ComprehensiveCogDialSignalApi signal_api{};

  const auto start_time = jewels::time::SyncTime{std::chrono::nanoseconds{1'000'000'000}}; // 1 second
  const auto end_time = jewels::time::SyncTime{std::chrono::nanoseconds{3'000'000'000}};   // 3 seconds
  const auto metadata1 = jewels::time::SyncTime{std::chrono::nanoseconds{1'500'000'000}};

  // Start window and accumulate data
  ComprehensiveCogPolicy::start_of_execution_test_group(signal_api, start_time);
  signal_api.set_basic_signal(100U);
  signal_api.accumulate_combined_signal(std::chrono::milliseconds{10});
  signal_api.accumulate_combined_signal(std::chrono::milliseconds{20});
  signal_api.accumulate_mean_signal(30U);
  signal_api.accumulate_mean_signal(50U);
  signal_api.accumulate_metadata_preserved(-10, metadata1);
  signal_api.accumulate_metadata_stripped(500U, metadata1);
  ComprehensiveCogPolicy::end_of_execution_test_group(signal_api, end_time);

  // Create message and populate
  Tap<Tachyon<ComprehensiveCog_test_group>> msg{};
  ComprehensiveCogPolicy::populate_test_group(signal_api, msg);

  // Verify execution_count
  REQUIRE(msg.get_execution_count() == 1);

  // Verify execution_interval: end_time - start_time = 2 seconds
  REQUIRE(msg.get_execution_interval() == std::chrono::nanoseconds{2'000'000'000});

  // Verify post-aggregated signal values
  // basic_signal uses default VALUE pre-agg and MIN post-agg
  REQUIRE(msg.get_basic_signal_value_min() == 100U);

  // combined_signal uses MIN pre-agg with MIN and FINAL_VALUE post-agg
  REQUIRE(msg.get_combined_signal_min_min() == std::chrono::milliseconds{10});
  REQUIRE(
    msg.get_combined_signal_min_final_value() == std::chrono::milliseconds{10}); // Only one min value per execution

  // mean_signal uses SUM pre-agg with MEAN and MAX post-agg
  // Single execution: sum = 30 + 50 = 80
  // MEAN across 1 execution = 80 / 1 = 80.0
  REQUIRE(msg.get_mean_signal_sum_mean() == Catch::Approx(80.0));
  REQUIRE(msg.get_mean_signal_sum_max() == 80U); // max of the sums

  // metadata_preserved uses MIN pre-agg with MAX post-agg, with metadata
  REQUIRE(msg.get_metadata_preserved_min_max() == -10);
  REQUIRE(msg.get_metadata_preserved_min_max_metadata() == metadata1);

  // metadata_stripped uses SUM pre-agg with SUM post-agg (no metadata)
  REQUIRE(msg.get_metadata_stripped_sum_sum() == 500U);
}

TEST_CASE("ComprehensiveCog SignalApi populate_test_group with multiple executions", "[populate][post-aggregated]")
{
  ComprehensiveCogDialSignalApi signal_api{};

  const auto start_time = jewels::time::SyncTime{std::chrono::nanoseconds{0}};
  ComprehensiveCogPolicy::start_of_execution_test_group(signal_api, start_time);

  const auto metadata1 = jewels::time::SyncTime{std::chrono::nanoseconds{100}};
  const auto metadata2 = jewels::time::SyncTime{std::chrono::nanoseconds{200}};
  const auto metadata3 = jewels::time::SyncTime{std::chrono::nanoseconds{300}};

  // First execution
  signal_api.set_basic_signal(100U);
  signal_api.accumulate_combined_signal(std::chrono::milliseconds{50});
  signal_api.accumulate_combined_signal(std::chrono::milliseconds{60});
  signal_api.accumulate_mean_signal(10U);
  signal_api.accumulate_metadata_preserved(5, metadata1);
  signal_api.accumulate_metadata_stripped(100U, metadata1);
  ComprehensiveCogPolicy::end_of_execution_test_group(
    signal_api, jewels::time::SyncTime{std::chrono::nanoseconds{1'000'000}});

  // Second execution
  signal_api.set_basic_signal(80U);
  signal_api.accumulate_combined_signal(std::chrono::milliseconds{30});
  signal_api.accumulate_combined_signal(std::chrono::milliseconds{40});
  signal_api.accumulate_mean_signal(20U);
  signal_api.accumulate_metadata_preserved(15, metadata2);
  signal_api.accumulate_metadata_stripped(200U, metadata2);
  ComprehensiveCogPolicy::end_of_execution_test_group(
    signal_api, jewels::time::SyncTime{std::chrono::nanoseconds{2'000'000}});

  // Third execution
  signal_api.set_basic_signal(120U);
  signal_api.accumulate_combined_signal(std::chrono::milliseconds{70});
  signal_api.accumulate_mean_signal(5U);
  signal_api.accumulate_metadata_preserved(-5, metadata3);
  signal_api.accumulate_metadata_stripped(300U, metadata3);
  const auto final_end_time = jewels::time::SyncTime{std::chrono::nanoseconds{3'000'000}};
  ComprehensiveCogPolicy::end_of_execution_test_group(signal_api, final_end_time);

  // Create message and populate
  Tap<Tachyon<ComprehensiveCog_test_group>> msg{};
  ComprehensiveCogPolicy::populate_test_group(signal_api, msg);

  // Verify execution_count
  REQUIRE(msg.get_execution_count() == 3);

  // Verify execution_interval
  REQUIRE(msg.get_execution_interval() == std::chrono::nanoseconds{3'000'000});

  // Verify post-aggregated values
  // basic_signal: MIN(100, 80, 120) = 80
  REQUIRE(msg.get_basic_signal_value_min() == 80U);

  // combined_signal: MIN of each execution is 50, 30, 70
  // MIN of mins = 30, FINAL_VALUE = 70 (last min)
  REQUIRE(msg.get_combined_signal_min_min() == std::chrono::milliseconds{30});
  REQUIRE(msg.get_combined_signal_min_final_value() == std::chrono::milliseconds{70});

  // mean_signal: SUM of each execution is 10, 20, 5
  // MEAN = (10+20+5)/3 = 35/3 = 11.666...
  REQUIRE(msg.get_mean_signal_sum_mean() == Catch::Approx(35.0 / 3.0).epsilon(0.01));
  REQUIRE(msg.get_mean_signal_sum_max() == 20U);

  // metadata_preserved: MIN of each execution is 5, 15, -5
  // MAX of mins = 15 with metadata2
  REQUIRE(msg.get_metadata_preserved_min_max() == 15);
  REQUIRE(msg.get_metadata_preserved_min_max_metadata() == metadata2);

  // metadata_stripped: SUM of each execution is 100, 200, 300
  // SUM of sums = 600
  REQUIRE(msg.get_metadata_stripped_sum_sum() == 600U);
}

TEST_CASE("ComprehensiveCog SignalApi populate_test_group with max executions", "[populate][post-aggregated]")
{
  ComprehensiveCogDialSignalApi signal_api{};

  const auto start_time = jewels::time::SyncTime{std::chrono::nanoseconds{0}};
  ComprehensiveCogPolicy::start_of_execution_test_group(signal_api, start_time);

  // Accumulate max_observations (100) executions
  for (size_t i = 0; i < 100; ++i)
  {
    signal_api.set_basic_signal(static_cast<uint64_t>(i + 10));
    signal_api.accumulate_mean_signal(static_cast<uint32_t>(i));
    const auto end_time = jewels::time::SyncTime{std::chrono::nanoseconds{(i + 1) * 1000}};
    ComprehensiveCogPolicy::end_of_execution_test_group(signal_api, end_time);
  }

  // Create message and populate
  Tap<Tachyon<ComprehensiveCog_test_group>> msg{};
  ComprehensiveCogPolicy::populate_test_group(signal_api, msg);

  // Verify execution_count is 100
  REQUIRE(msg.get_execution_count() == 100);

  // Verify execution_interval
  REQUIRE(msg.get_execution_interval() == std::chrono::nanoseconds{100'000});

  // Verify post-aggregated values
  // basic_signal: MIN(10, 11, 12, ..., 109) = 10
  REQUIRE(msg.get_basic_signal_value_min() == 10U);

  // mean_signal: Each execution accumulates i (0 to 99), so sum per execution = i
  // Total = 0+1+2+...+99 = 4950
  // MEAN = 4950 / 100 = 49.5
  REQUIRE(msg.get_mean_signal_sum_mean() == Catch::Approx(49.5).epsilon(0.01));
  REQUIRE(msg.get_mean_signal_sum_max() == 99U);
}

} // namespace clockwork::dsl::tests::support
