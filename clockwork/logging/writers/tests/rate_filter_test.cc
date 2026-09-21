// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/writers/rate_filter.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/time/sync_time.hh"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <memory_resource>

namespace clockwork_logging
{
namespace
{

TEST_CASE("RateFilter")
{
  static constexpr size_t window_size = 3U;
  constexpr jewels::time::SteadyTime logtime1{std::chrono::seconds(1)};
  constexpr jewels::time::SteadyTime logtime2{std::chrono::seconds(2)};
  constexpr jewels::time::SteadyTime logtime3{std::chrono::seconds(3)};
  constexpr jewels::time::SteadyTime logtime4{std::chrono::seconds(4)};
  constexpr jewels::time::SteadyTime logtime5{std::chrono::seconds(5)};
  constexpr jewels::time::SteadyTime logtime6{std::chrono::seconds(6)};
  constexpr jewels::time::SteadyTime logtime7{std::chrono::seconds(7)};
  constexpr jewels::time::SteadyTime logtime8{std::chrono::seconds(8)};
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};

  SECTION("Invalid window size")
  {
    REQUIRE_THROWS(RateFilter{memory_resource, logtime1, 0U});
  }

  SECTION("Empty filter")
  {
    RateFilter rate_filter{memory_resource, logtime1, window_size};
    REQUIRE(rate_filter.get_rate(logtime1) == 0.0);
  }

  SECTION("Rolling average")
  {
    RateFilter rate_filter{memory_resource, logtime1, window_size};
    REQUIRE(rate_filter.get_rate(logtime1) == 0.0);
    rate_filter.update(logtime1, 3U);
    REQUIRE(rate_filter.get_rate(logtime1) == 0.0);

    REQUIRE(rate_filter.get_rate(logtime2) == 1.0);
    rate_filter.update(logtime2, 6U);
    REQUIRE(rate_filter.get_rate(logtime2) == 1.0);

    REQUIRE(rate_filter.get_rate(logtime3) == 3.0);
    rate_filter.update(logtime3, 9U);
    REQUIRE(rate_filter.get_rate(logtime3) == 3.0);

    rate_filter.update(logtime4, 12U);
    REQUIRE(rate_filter.get_rate(logtime4) == 6.0);

    REQUIRE(rate_filter.get_rate(logtime5) == 9.0);

    REQUIRE(rate_filter.get_rate(logtime6) == 7.0);

    REQUIRE(rate_filter.get_rate(logtime7) == 4.0);

    REQUIRE(rate_filter.get_rate(logtime8) == 0.0);
  }

  SECTION("Handle time jumps correctly")
  {
    RateFilter rate_filter{memory_resource, logtime1, window_size};
    rate_filter.update(logtime1, 3U);

    rate_filter.update(logtime2, 6U);

    rate_filter.update(logtime3, 9U);

    rate_filter.update(logtime4, 12U);
    REQUIRE(rate_filter.get_rate(logtime4) == 6.0);

    SECTION("Time jumps backward clears filter")
    {
      REQUIRE(rate_filter.get_rate(logtime3) == 0.0);
    }

    SECTION("Time advances 1 second")
    {
      REQUIRE(rate_filter.get_rate(logtime5) == 9.0);
    }

    SECTION("Time advances 2 seconds")
    {
      REQUIRE(rate_filter.get_rate(logtime6) == 7.0);
    }

    SECTION("Time advances 3 seconds")
    {
      REQUIRE(rate_filter.get_rate(logtime7) == 4.0);
    }

    SECTION("Time advances 4 seconds")
    {
      REQUIRE(rate_filter.get_rate(logtime8) == 0.0);
    }
  }
}

} // namespace
} // namespace clockwork_logging
