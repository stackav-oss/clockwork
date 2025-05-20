// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/log_timestamp.hh"
#include "jewels/time/sync_time.hh"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <sstream> // IWYU pragma: keep
#include <string>

namespace clockwork_logging::tests
{
namespace
{

TEST_CASE("LogTimestamp")
{
  SECTION("Constructors/accessors")
  {
    const LogTimestamp time1{1'234'567};
    CHECK(time1.get_nanoseconds() == 1'234'567);
    CHECK(time1.get_duration() == std::chrono::nanoseconds{1'234'567});
    CHECK(time1.get_time() == jewels::time::SyncTime{std::chrono::nanoseconds{1'234'567}});
    const LogTimestamp time2{std::chrono::nanoseconds{2'345'678}};
    CHECK(time2.get_nanoseconds() == 2'345'678);
    CHECK(time2.get_duration() == std::chrono::nanoseconds{2'345'678});
    CHECK(time2.get_time() == jewels::time::SyncTime{std::chrono::nanoseconds{2'345'678}});
    const LogTimestamp time3{jewels::time::SyncTime{std::chrono::nanoseconds{3'456'789}}};
    CHECK(time3.get_nanoseconds() == 3'456'789);
    CHECK(time3.get_duration() == std::chrono::nanoseconds{3'456'789});
    CHECK(time3.get_time() == jewels::time::SyncTime{std::chrono::nanoseconds{3'456'789}});
  }

  SECTION("operator <<")
  {
    std::stringstream sstream;
    sstream << LogTimestamp{1'234'567}; // NOLINT(cert-err33-c) False positive
    CHECK(sstream.str() == "1234567");
  }

  SECTION("operator +=")
  {
    LogTimestamp time1{2'345'678};
    time1 += std::chrono::nanoseconds{1'111'111};
    CHECK(time1.get_nanoseconds() == 3'456'789);
  }

  SECTION("operator +")
  {
    const auto time1 = LogTimestamp{2'345'678} + std::chrono::nanoseconds{1'111'111};
    CHECK(time1.get_nanoseconds() == 3'456'789);
  }

  SECTION("operator -=")
  {
    LogTimestamp time1{2'345'678};
    time1 -= std::chrono::nanoseconds{1'111'111};
    CHECK(time1.get_nanoseconds() == 1'234'567);
  }

  SECTION("operator -")
  {
    const auto time1 = LogTimestamp{2'345'678} - std::chrono::nanoseconds{1'111'111};
    CHECK(time1.get_nanoseconds() == 1'234'567);
    const auto duration2 = LogTimestamp{2'345'678} - LogTimestamp{1'111'111};
    CHECK(duration2.count() == 1'234'567);
  }

  SECTION("Comparison operators")
  {
    const LogTimestamp time1{1'234'567};
    const LogTimestamp time2{2'345'678};

    CHECK(time1 == time1);
    CHECK_FALSE(time1 == time2);
    CHECK_FALSE(time2 == time1);
    CHECK(time2 == time2);

    CHECK_FALSE(time1 != time1);
    CHECK(time1 != time2);
    CHECK(time2 != time1);
    CHECK_FALSE(time2 != time2);

    CHECK_FALSE(time1 < time1);
    CHECK(time1 < time2);
    CHECK_FALSE(time2 < time1);
    CHECK_FALSE(time2 < time2);

    CHECK(time1 <= time1);
    CHECK(time1 <= time2);
    CHECK_FALSE(time2 <= time1);
    CHECK(time2 <= time2);

    CHECK_FALSE(time1 > time1);
    CHECK_FALSE(time1 > time2);
    CHECK(time2 > time1);
    CHECK_FALSE(time2 > time2);

    CHECK(time1 >= time1);
    CHECK_FALSE(time1 >= time2);
    CHECK(time2 >= time1);
    CHECK(time2 >= time2);
  }
}

} // namespace
} // namespace clockwork_logging::tests
