// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dial/signal_aggregator.hh"
#include "jewels/time/sync_time.hh"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <limits>

namespace clockwork
{
namespace
{
using jewels::time::SyncTime;
using namespace std::chrono_literals;

//==============================================================================
// MinAggregator Tests
//==============================================================================
TEST_CASE("MinAggregator")
{
  MinAggregator<int32_t> agg;
  REQUIRE_FALSE(agg.has_value());

  // Finds minimum across positive, negative, and edge values
  agg.accumulate(100);
  agg.accumulate(-50);
  agg.accumulate(75);
  agg.accumulate(std::numeric_limits<int32_t>::max());
  REQUIRE(agg.has_value());
  REQUIRE(agg.get_value() == -50);

  // Edge case: minimum possible value
  agg.accumulate(std::numeric_limits<int32_t>::min());
  REQUIRE(agg.get_value() == std::numeric_limits<int32_t>::min());
}

//==============================================================================
// MaxAggregator Tests
//==============================================================================
TEST_CASE("MaxAggregator")
{
  MaxAggregator<int32_t> agg;
  REQUIRE_FALSE(agg.has_value());

  // Finds maximum across positive, negative, and edge values
  agg.accumulate(50);
  agg.accumulate(-10);
  agg.accumulate(75);
  agg.accumulate(std::numeric_limits<int32_t>::min());
  REQUIRE(agg.has_value());
  REQUIRE(agg.get_value() == 75);

  // Edge case: maximum possible value
  agg.accumulate(std::numeric_limits<int32_t>::max());
  REQUIRE(agg.get_value() == std::numeric_limits<int32_t>::max());
}

//==============================================================================
// SumAggregator Tests
//==============================================================================
TEST_CASE("SumAggregator")
{
  SECTION("Integer accumulation")
  {
    SumAggregator<int32_t> agg;
    REQUIRE_FALSE(agg.has_value());

    agg.accumulate(10);
    agg.accumulate(20);
    agg.accumulate(30);
    REQUIRE(agg.has_value());
    REQUIRE(agg.get_value() == 60);
  }

  SECTION("Floating point accumulation")
  {
    SumAggregator<double> agg;
    agg.accumulate(1.5);
    agg.accumulate(2.5);
    agg.accumulate(3.0);
    REQUIRE(agg.get_value() == 7.0);
  }
}

//==============================================================================
// CountAggregator Tests
//==============================================================================
TEST_CASE("CountAggregator")
{
  CountAggregator<uint64_t> agg;
  REQUIRE_FALSE(agg.has_value());
  REQUIRE(agg.get_value() == 0);

  // Counts accumulations regardless of value
  agg.accumulate(42);
  agg.accumulate(100);
  agg.accumulate(-5);
  REQUIRE(agg.get_value() == 3);
  REQUIRE(agg.has_value());
}

//==============================================================================
// MeanAggregator Tests
//==============================================================================
TEST_CASE("MeanAggregator")
{
  SECTION("Floating point mean")
  {
    MeanAggregator<double> agg;
    REQUIRE_FALSE(agg.has_value());
    REQUIRE(agg.get_mean() == 0.0);

    agg.accumulate(10.0);
    agg.accumulate(20.0);
    agg.accumulate(30.0);
    REQUIRE(agg.has_value());
    REQUIRE(agg.get_sum() == 60.0);
    REQUIRE(agg.get_count() == 3);
    REQUIRE(agg.get_mean() == 20.0);
  }

  SECTION("Integer mean with truncation")
  {
    MeanAggregator<int32_t> agg;
    agg.accumulate(10);
    agg.accumulate(20);
    agg.accumulate(33);
    // Integer division: 63 / 3 = 21
    REQUIRE(agg.get_mean() == 21);
  }
}

//==============================================================================
// FirstValueAggregator Tests
//==============================================================================
TEST_CASE("FirstValueAggregator")
{
  FirstValueAggregator<int32_t> agg;
  REQUIRE_FALSE(agg.has_value());

  // Keeps first value only
  agg.accumulate(100);
  agg.accumulate(200);
  agg.accumulate(300);
  REQUIRE(agg.has_value());
  REQUIRE(agg.get_value() == 100);
}

//==============================================================================
// FinalValueAggregator Tests
//==============================================================================
TEST_CASE("FinalValueAggregator")
{
  FinalValueAggregator<int32_t> agg;
  REQUIRE_FALSE(agg.has_value());

  // Keeps last value
  agg.accumulate(100);
  agg.accumulate(200);
  agg.accumulate(300);
  REQUIRE(agg.has_value());
  REQUIRE(agg.get_value() == 300);
}

//==============================================================================
// Metadata Preservation Tests
//==============================================================================
TEST_CASE("Aggregators with metadata")
{
  SECTION("MinAggregator preserves metadata of minimum")
  {
    MinAggregator<int32_t, SyncTime> agg;
    agg.accumulate(100, SyncTime(100ms));
    agg.accumulate(50, SyncTime(200ms));
    agg.accumulate(75, SyncTime(300ms));
    REQUIRE(agg.get_value() == 50);
    REQUIRE(agg.get_metadata() == SyncTime(200ms));
  }

  SECTION("MaxAggregator preserves metadata of maximum")
  {
    MaxAggregator<int32_t, SyncTime> agg;
    agg.accumulate(100, SyncTime(100ms));
    agg.accumulate(50, SyncTime(200ms));
    agg.accumulate(75, SyncTime(300ms));
    REQUIRE(agg.get_value() == 100);
    REQUIRE(agg.get_metadata() == SyncTime(100ms));
  }

  SECTION("FirstValueAggregator preserves first metadata")
  {
    FirstValueAggregator<int32_t, SyncTime> agg;
    agg.accumulate(100, SyncTime(100ms));
    agg.accumulate(200, SyncTime(200ms));
    REQUIRE(agg.get_value() == 100);
    REQUIRE(agg.get_metadata() == SyncTime(100ms));
  }

  SECTION("FinalValueAggregator preserves final metadata")
  {
    FinalValueAggregator<int32_t, SyncTime> agg;
    agg.accumulate(100, SyncTime(100ms));
    agg.accumulate(200, SyncTime(200ms));
    agg.accumulate(300, SyncTime(300ms));
    REQUIRE(agg.get_value() == 300);
    REQUIRE(agg.get_metadata() == SyncTime(300ms));
  }
}

//==============================================================================
// Reset Functionality Tests
//==============================================================================
TEST_CASE("Reset clears aggregator state")
{
  SECTION("MinAggregator")
  {
    MinAggregator<int32_t> agg;
    agg.accumulate(42);
    REQUIRE(agg.has_value());
    agg.reset();
    REQUIRE_FALSE(agg.has_value());
  }

  SECTION("MaxAggregator")
  {
    MaxAggregator<int32_t> agg;
    agg.accumulate(42);
    REQUIRE(agg.has_value());
    agg.reset();
    REQUIRE_FALSE(agg.has_value());
  }

  SECTION("SumAggregator")
  {
    SumAggregator<int32_t> agg;
    agg.accumulate(42);
    REQUIRE(agg.has_value());
    agg.reset();
    REQUIRE_FALSE(agg.has_value());
    REQUIRE(agg.get_value() == 0);
  }

  SECTION("CountAggregator")
  {
    CountAggregator<uint64_t> agg;
    agg.accumulate(1);
    agg.accumulate(2);
    agg.reset();
    REQUIRE_FALSE(agg.has_value());
    REQUIRE(agg.get_value() == 0);
  }

  SECTION("MeanAggregator")
  {
    MeanAggregator<double> agg;
    agg.accumulate(42.0);
    agg.reset();
    REQUIRE_FALSE(agg.has_value());
    REQUIRE(agg.get_sum() == 0.0);
    REQUIRE(agg.get_count() == 0);
  }

  SECTION("FirstValueAggregator allows new first value after reset")
  {
    FirstValueAggregator<int32_t> agg;
    agg.accumulate(100);
    agg.reset();
    agg.accumulate(200);
    REQUIRE(agg.get_value() == 200);
  }

  SECTION("FinalValueAggregator")
  {
    FinalValueAggregator<int32_t> agg;
    agg.accumulate(100);
    agg.reset();
    REQUIRE_FALSE(agg.has_value());
  }
}

} // namespace
} // namespace clockwork
