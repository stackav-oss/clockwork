// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"

#include <catch2/catch_test_macros.hpp>

#include <sstream> // IWYU pragma: keep
#include <string>

namespace clockwork_logging::tests
{
namespace
{

TEST_CASE("LogInterval")
{
  constexpr auto time1 = LogTimestamp(1);
  constexpr auto time2 = LogTimestamp(2);
  constexpr auto time3 = LogTimestamp(3);
  constexpr auto time4 = LogTimestamp(4);
  constexpr auto time5 = LogTimestamp(5);

  SECTION("Smoke test")
  {
    const LogInterval interval1(time1);
    const LogInterval interval2(time2, time3);
    const LogInterval interval3(time5, time4);
    const LogInterval interval4(time1, time5);

    // Time constructors
    CHECK(interval1 == LogInterval{time1.get_time()});
    CHECK(interval2 == LogInterval{time2.get_time(), time3.get_time()});
    CHECK(interval3 == LogInterval{time5.get_time(), time4.get_time()});
    CHECK(interval4 == LogInterval{time1.get_time(), time5.get_time()});

    // get_start/end_timestamp
    CHECK(interval1.get_start_timestamp() == time1);
    CHECK(interval1.get_end_timestamp() == time1);

    CHECK(interval2.get_start_timestamp() == time2);
    CHECK(interval2.get_end_timestamp() == time3);

    CHECK(interval3.get_start_timestamp() == time4);
    CHECK(interval3.get_end_timestamp() == time5);

    CHECK(interval4.get_start_timestamp() == time1);
    CHECK(interval4.get_end_timestamp() == time5);

    // get_start/end_time
    CHECK(interval1.get_start_time() == time1.get_time());
    CHECK(interval1.get_end_time() == time1.get_time());

    CHECK(interval2.get_start_time() == time2.get_time());
    CHECK(interval2.get_end_time() == time3.get_time());

    CHECK(interval3.get_start_time() == time4.get_time());
    CHECK(interval3.get_end_time() == time5.get_time());

    CHECK(interval4.get_start_time() == time1.get_time());
    CHECK(interval4.get_end_time() == time5.get_time());

    // get_duration
    CHECK(interval1.get_duration() == std::chrono::nanoseconds(0));
    CHECK(interval2.get_duration() == (time3 - time2));
    CHECK(interval3.get_duration() == (time5 - time4));
    CHECK(interval4.get_duration() == (time5 - time1));

    // operator==
    CHECK(interval1 == interval1);
    CHECK_FALSE(interval1 == interval2);
    CHECK_FALSE(interval1 == interval3);
    CHECK_FALSE(interval1 == interval4);

    CHECK_FALSE(interval2 == interval1);
    CHECK(interval2 == interval2);
    CHECK_FALSE(interval2 == interval3);
    CHECK_FALSE(interval2 == interval4);

    CHECK_FALSE(interval3 == interval1);
    CHECK_FALSE(interval3 == interval2);
    CHECK(interval3 == interval3);
    CHECK_FALSE(interval3 == interval4);

    CHECK_FALSE(interval4 == interval1);
    CHECK_FALSE(interval4 == interval2);
    CHECK_FALSE(interval4 == interval3);
    CHECK(interval4 == interval4);

    // operator!=
    CHECK_FALSE(interval1 != interval1);
    CHECK(interval1 != interval2);
    CHECK(interval1 != interval3);
    CHECK(interval1 != interval4);

    CHECK(interval2 != interval1);
    CHECK_FALSE(interval2 != interval2);
    CHECK(interval2 != interval3);
    CHECK(interval2 != interval4);

    CHECK(interval3 != interval1);
    CHECK(interval3 != interval2);
    CHECK_FALSE(interval3 != interval3);
    CHECK(interval3 != interval4);

    CHECK(interval4 != interval1);
    CHECK(interval4 != interval2);
    CHECK(interval4 != interval3);
    CHECK_FALSE(interval4 != interval4);
  }

  SECTION("operator <<")
  {
    std::stringstream sstream;
    sstream << LogInterval(time1, time5); // NOLINT(cert-err33-c) False positive
    CHECK(sstream.str() == "{1, 5}");
  }

  SECTION("add_timestamp")
  {
    LogInterval interval(time3);
    CHECK(interval == LogInterval(time3, time3));
    interval.add_timestamp(time5);
    CHECK(interval == LogInterval(time3, time5));
    interval.add_timestamp(time4);
    CHECK(interval == LogInterval(time3, time5));
    interval.add_timestamp(time1);
    CHECK(interval == LogInterval(time1, time5));
    interval.add_timestamp(time2);
    CHECK(interval == LogInterval(time1, time5));
  }

  SECTION("add_time")
  {
    LogInterval interval(time3.get_time());
    CHECK(interval == LogInterval(time3, time3));
    interval.add_time(time5.get_time());
    CHECK(interval == LogInterval(time3, time5));
    interval.add_time(time4.get_time());
    CHECK(interval == LogInterval(time3, time5));
    interval.add_time(time1.get_time());
    CHECK(interval == LogInterval(time1, time5));
    interval.add_time(time2.get_time());
    CHECK(interval == LogInterval(time1, time5));
  }

  SECTION("add_interval")
  {
    LogInterval interval1(time3);
    CHECK(interval1 == LogInterval(time3, time3));
    interval1.add_interval({time2, time4});
    CHECK(interval1 == LogInterval(time2, time4));

    LogInterval interval2(time3);
    CHECK(interval2 == LogInterval(time3, time3));
    interval2.add_interval({time4, time5});
    CHECK(interval2 == LogInterval(time3, time5));

    LogInterval interval3(time3);
    CHECK(interval3 == LogInterval(time3, time3));
    interval3.add_interval({time1, time2});
    CHECK(interval3 == LogInterval(time1, time3));
  }

  SECTION("contains")
  {
    CHECK_FALSE(LogInterval(time2, time4).contains(time1));
    CHECK(LogInterval(time2, time4).contains(time2));
    CHECK(LogInterval(time2, time4).contains(time3));
    CHECK(LogInterval(time2, time4).contains(time4));
    CHECK_FALSE(LogInterval(time2, time4).contains(time5));
    CHECK_FALSE(LogInterval(time2, time4).contains(time1.get_time()));
    CHECK(LogInterval(time2, time4).contains(time2.get_time()));
    CHECK(LogInterval(time2, time4).contains(time3.get_time()));
    CHECK(LogInterval(time2, time4).contains(time4.get_time()));
    CHECK_FALSE(LogInterval(time2, time4).contains(time5.get_time()));
  }

  SECTION("overlaps")
  {
    CHECK(LogInterval(time3, time3).overlaps(LogInterval(time3)));
    CHECK(LogInterval(time1, time2).overlaps(LogInterval(time2, time3)));
    CHECK(LogInterval(time2, time3).overlaps(LogInterval(time1, time2)));
    CHECK(LogInterval(time2, time3).overlaps(LogInterval(time1, time5)));
    CHECK(LogInterval(time1, time5).overlaps(LogInterval(time2, time3)));
    CHECK_FALSE(LogInterval(time1, time3).overlaps(LogInterval(time4, time5)));
    CHECK_FALSE(LogInterval(time4, time5).overlaps(LogInterval(time1, time2)));
  }
}

} // namespace
} // namespace clockwork_logging::tests
