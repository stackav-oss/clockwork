// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/rate_limiter/time_bucket.hh"
#include "jewels/time/sync_time.hh"

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>

namespace jewels::rate_limiter
{

TEMPLATE_TEST_CASE("TimeBucket", "", TimeBucket, ThreadSafeTimeBucket)
{
  using TimeBucketType = TestType;

  TimeBucketType limiter{std::chrono::seconds(1), std::chrono::milliseconds(100)};
  auto now = time::SyncClock::now();
  auto throttled_until = time::SyncTime::min();

  // Quickly exhaust the bucket credits
  for (auto i = 0; i < 10; i++)
  {
    REQUIRE(ok(limiter.check_credit(Out{throttled_until}, now)));
    now += std::chrono::milliseconds{1U};
  }

  // We've reached the credit limit. Token acquisition should fail until enough time
  // passes.
  REQUIRE(fails(limiter.check_credit(Out{throttled_until}, now)));
  const auto first_deadline = now + std::chrono::milliseconds{90};
  REQUIRE(throttled_until == first_deadline);
  now += std::chrono::milliseconds{10U};
  REQUIRE(fails(limiter.check_credit(Out{throttled_until}, now)));
  REQUIRE(throttled_until == first_deadline);

  // Jump forward enough to earn enough credits for 5 units of work
  now += std::chrono::milliseconds{489U};
  for (auto i = 0; i < 5; i++)
  {
    REQUIRE(ok(limiter.check_credit(Out{throttled_until}, now)));
    now += std::chrono::milliseconds{1U};
  }
  REQUIRE(fails(limiter.check_credit(Out{throttled_until}, now)));

  // You shouldn't be able to accumulate more credits than the credit limit.
  now += std::chrono::minutes{60};
  for (auto i = 0; i < 10; i++)
  {
    REQUIRE(ok(limiter.check_credit(Out{throttled_until}, now)));
    now += std::chrono::milliseconds{1U};
  }
  REQUIRE(fails(limiter.check_credit(Out{throttled_until}, now)));

  // You shouldn't be able to cheat by jumping backwards.
  now -= std::chrono::seconds{10};
  REQUIRE(fails(limiter.check_credit(Out{throttled_until}, now)));
  now += std::chrono::seconds{10};
  REQUIRE(fails(limiter.check_credit(Out{throttled_until}, now)));
}

TEMPLATE_TEST_CASE("Rejected checks do not consume credit", "", TimeBucket, ThreadSafeTimeBucket)
{
  using TimeBucketType = TestType;

  TimeBucketType limiter{std::chrono::nanoseconds{10}, std::chrono::nanoseconds{6}};
  const auto now = time::SyncTime{std::chrono::seconds{1}};
  auto throttled_until = time::SyncTime::min();
  REQUIRE(ok(limiter.check_credit(Out{throttled_until}, now)));
  REQUIRE(fails(limiter.check_credit(Out{throttled_until}, now)));
  REQUIRE(throttled_until == now + std::chrono::nanoseconds{2});
  REQUIRE(fails(limiter.check_credit(Out{throttled_until}, now)));
  REQUIRE(throttled_until == now + std::chrono::nanoseconds{2});
  REQUIRE(ok(limiter.check_credit(Out{throttled_until}, now + std::chrono::nanoseconds{2})));
}

} // namespace jewels::rate_limiter
