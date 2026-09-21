// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dial/include_common.hh"
#include "clockwork/dsl/ir/tests/support/report_group_defs_clk_cc_dial.hh"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <concepts>
#include <cstdint>
#include <span>

namespace clockwork::dsl::tests::support
{

// Concept to check if a getter exists for a specific field
template <typename TapType, typename FieldType, auto getter_func>
concept HasFieldGetter = requires(const TapType& tap) {
  { (tap.*getter_func)() } -> std::same_as<const FieldType&>;
};

// Test that the report group schema was generated
TEST_CASE("Report group schema exists and has expected structure", "[report_group]")
{
  using TapType = ::clockwork::Tap<::clockwork::Tachyon<ComprehensiveCog_test_group>>;

  // Verify common fields exist
  STATIC_REQUIRE(HasFieldGetter<TapType, uint16_t, &TapType::get_execution_count>);
  STATIC_REQUIRE(HasFieldGetter<TapType, std::chrono::nanoseconds, &TapType::get_execution_interval>);
}

// Test that basic_signal post-aggregated fields exist (min, max)
TEST_CASE("basic_signal post-aggregation fields exist", "[report_group][basic_signal]")
{
  using TapType = ::clockwork::Tap<::clockwork::Tachyon<ComprehensiveCog_test_group>>;

  // basic_signal has post_aggregation: ["min", "max"]
  // Since it has no pre_aggregation, field name is: basic_signal_value_{post_agg}
  STATIC_REQUIRE(HasFieldGetter<TapType, uint64_t, &TapType::get_basic_signal_value_min>);
  STATIC_REQUIRE(HasFieldGetter<TapType, uint64_t, &TapType::get_basic_signal_value_max>);
}

// Test that combined_signal fields exist with both pre and post aggregation
TEST_CASE("combined_signal pre and post-aggregation fields exist", "[report_group][combined_signal]")
{
  using TapType = ::clockwork::Tap<::clockwork::Tachyon<ComprehensiveCog_test_group>>;

  // combined_signal has:
  //   pre_aggregation: ["min", "max"]
  //   post_aggregation: ["min", "final_value"]
  // Field names: combined_signal_{pre_agg}_{post_agg}
  STATIC_REQUIRE(HasFieldGetter<TapType, std::chrono::nanoseconds, &TapType::get_combined_signal_min_min>);
  STATIC_REQUIRE(HasFieldGetter<TapType, std::chrono::nanoseconds, &TapType::get_combined_signal_min_final_value>);
  STATIC_REQUIRE(HasFieldGetter<TapType, std::chrono::nanoseconds, &TapType::get_combined_signal_max_min>);
  STATIC_REQUIRE(HasFieldGetter<TapType, std::chrono::nanoseconds, &TapType::get_combined_signal_max_final_value>);
}

// Test that mean aggregation converts type to Float32
TEST_CASE("mean_signal fields exist with correct types", "[report_group][mean_signal]")
{
  using TapType = ::clockwork::Tap<::clockwork::Tachyon<ComprehensiveCog_test_group>>;

  // mean_signal is UInt32 with:
  //   pre_aggregation: ["sum"]
  //   post_aggregation: ["mean", "max"]
  // "mean" converts to Float32 (float), "max" stays UInt32
  STATIC_REQUIRE(HasFieldGetter<TapType, float, &TapType::get_mean_signal_sum_mean>);
  STATIC_REQUIRE(HasFieldGetter<TapType, uint32_t, &TapType::get_mean_signal_sum_max>);
}

// Test that metadata is preserved when appropriate
TEST_CASE("metadata preservation in aggregation", "[report_group][metadata]")
{
  using TapType = ::clockwork::Tap<::clockwork::Tachyon<ComprehensiveCog_test_group>>;

  // metadata_preserved has:
  //   metadata: SyncTime
  //   pre_aggregation: ["min"]  (preserves metadata)
  //   post_aggregation: ["max"]  (preserves metadata)
  // Both value and metadata fields should exist
  STATIC_REQUIRE(HasFieldGetter<TapType, int64_t, &TapType::get_metadata_preserved_min_max>);
  STATIC_REQUIRE(HasFieldGetter<TapType, ::jewels::time::SyncTime, &TapType::get_metadata_preserved_min_max_metadata>);

  // metadata_stripped has:
  //   metadata: SyncTime
  //   pre_aggregation: ["sum"]  (strips metadata)
  //   post_aggregation: ["sum"]  (strips metadata)
  // Only value field should exist, no metadata field
  STATIC_REQUIRE(HasFieldGetter<TapType, uint32_t, &TapType::get_metadata_stripped_sum_sum>);
}

// Integration test: verify all fields can be set and retrieved
TEST_CASE("All report group fields are functional", "[report_group][integration]")
{
  using TapType = ::clockwork::Tap<::clockwork::Tachyon<ComprehensiveCog_test_group>>;

  TapType tap;

  // Set values
  tap.set_execution_count(50);
  tap.set_execution_interval(std::chrono::seconds(5));
  tap.set_basic_signal_value_min(100);
  tap.set_basic_signal_value_max(200);
  tap.set_combined_signal_min_min(std::chrono::milliseconds(5));
  tap.set_combined_signal_min_final_value(std::chrono::milliseconds(10));
  tap.set_mean_signal_sum_mean(42.5);
  tap.set_metadata_preserved_min_max(-500);
  tap.set_metadata_preserved_min_max_metadata(::jewels::time::SyncTime{std::chrono::nanoseconds{123456789}});

  // Verify values
  REQUIRE(tap.get_execution_count() == 50);
  REQUIRE(tap.get_execution_interval() == std::chrono::seconds(5));
  REQUIRE(tap.get_basic_signal_value_min() == 100);
  REQUIRE(tap.get_basic_signal_value_max() == 200);
  REQUIRE(tap.get_combined_signal_min_min() == std::chrono::milliseconds(5));
  REQUIRE(tap.get_combined_signal_min_final_value() == std::chrono::milliseconds(10));
  REQUIRE(tap.get_mean_signal_sum_mean() == 42.5);
  REQUIRE(tap.get_metadata_preserved_min_max() == -500);
  REQUIRE(tap.get_metadata_preserved_min_max_metadata().time_since_epoch().count() == 123456789);
}

// Test that the batched report group outer schema was generated
TEST_CASE("Batched report group outer schema exists and has expected structure", "[report_group][batched]")
{
  using TapType = ::clockwork::Tap<::clockwork::Tachyon<BatchedCog_batched_group>>;

  STATIC_REQUIRE(HasFieldGetter<TapType, std::chrono::nanoseconds, &TapType::get_execution_interval>);
}

// Test that the batched inner SoA schema has correct fields
TEST_CASE("Batched report group inner SoA schema fields exist", "[report_group][batched]")
{
  using TapType = ::clockwork::Tap<::clockwork::Tachyon<BatchedCog_batched_group_Signal>>;

  // basic_batched with default pre-aggregation (value)
  STATIC_REQUIRE(HasFieldGetter<TapType, uint64_t, &TapType::get_basic_batched_value>);

  // multi_pre_agg with min and max pre-aggregation
  STATIC_REQUIRE(HasFieldGetter<TapType, int32_t, &TapType::get_multi_pre_agg_min>);
  STATIC_REQUIRE(HasFieldGetter<TapType, int32_t, &TapType::get_multi_pre_agg_max>);

  // mean_pre_agg is expanded to sum and count (mean is computed at query time)
  STATIC_REQUIRE(HasFieldGetter<TapType, uint16_t, &TapType::get_mean_pre_agg_sum>);
  STATIC_REQUIRE(HasFieldGetter<TapType, uint16_t, &TapType::get_mean_pre_agg_count>);

  // with_metadata preserves metadata for min aggregation
  STATIC_REQUIRE(HasFieldGetter<TapType, float, &TapType::get_with_metadata_min>);
  STATIC_REQUIRE(HasFieldGetter<TapType, ::jewels::time::SyncTime, &TapType::get_with_metadata_min_metadata>);

  // metadata_stripped has sum and count fields, no metadata (sum/count don't preserve metadata)
  STATIC_REQUIRE(HasFieldGetter<TapType, uint32_t, &TapType::get_metadata_stripped_sum>);
  STATIC_REQUIRE(HasFieldGetter<TapType, uint32_t, &TapType::get_metadata_stripped_count>);
}

// Integration test: verify batched report group VarSoa operations
TEST_CASE("Batched report group VarSoa is functional", "[report_group][batched][integration]")
{
  using TapType = ::clockwork::Tap<::clockwork::Tachyon<BatchedCog_batched_group>>;

  TapType tap;

  tap.set_execution_interval(std::chrono::nanoseconds{500000000});

  // Access the signals VarSoa and add some entries
  auto& signals = tap.get_mutable_signals();
  signals.resize(3);

  // Set values for first entry using setter methods
  signals[0].set_basic_batched_value(100);
  signals[0].set_multi_pre_agg_min(-50);
  signals[0].set_multi_pre_agg_max(50);
  signals[0].set_mean_pre_agg_sum(25);
  signals[0].set_mean_pre_agg_count(5);
  signals[0].set_with_metadata_min(1.5f);
  signals[0].set_with_metadata_min_metadata(::jewels::time::SyncTime{std::chrono::nanoseconds{111111111}});
  signals[0].set_metadata_stripped_sum(42);
  signals[0].set_metadata_stripped_count(2);

  // Set values for second entry
  signals[1].set_basic_batched_value(200);
  signals[1].set_multi_pre_agg_min(-100);
  signals[1].set_multi_pre_agg_max(100);
  signals[1].set_mean_pre_agg_sum(50);
  signals[1].set_mean_pre_agg_count(10);
  signals[1].set_with_metadata_min(2.5F); // NOLINT(readability-uppercase-literal-suffix)
  signals[1].set_with_metadata_min_metadata(::jewels::time::SyncTime{std::chrono::nanoseconds{222222222}});
  signals[1].set_metadata_stripped_sum(84);
  signals[1].set_metadata_stripped_count(4);

  // Verify values
  REQUIRE(tap.get_execution_interval() == std::chrono::nanoseconds{500000000});
  REQUIRE(signals.size() == 3);
  REQUIRE(signals[0].get_basic_batched_value() == 100);
  REQUIRE(signals[0].get_multi_pre_agg_min() == -50);
  REQUIRE(signals[0].get_multi_pre_agg_max() == 50);
  REQUIRE(signals[0].get_mean_pre_agg_sum() == 25);
  REQUIRE(signals[0].get_mean_pre_agg_count() == 5);
  REQUIRE(signals[0].get_with_metadata_min() == 1.5f);
  REQUIRE(signals[0].get_with_metadata_min_metadata().time_since_epoch().count() == 111111111);
  REQUIRE(signals[1].get_basic_batched_value() == 200);
  REQUIRE(signals[1].get_multi_pre_agg_min() == -100);
}

} // namespace clockwork::dsl::tests::support
