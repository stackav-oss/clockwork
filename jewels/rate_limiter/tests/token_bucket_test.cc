// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/rate_limiter/token_bucket.hh"
#include "jewels/time/sync_time.hh"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

namespace jewels::rate_limiter
{

TEST_CASE("TokenBucket")
{
  TokenBucket limiter(10U, time::SyncClock::duration{std::chrono::seconds{1}});
  auto now = time::SyncClock::now();
  auto throttled_until = time::SyncTime::min();

  // Quickly exhaust the entire pool of tokens.
  for (auto i = 0; i < 10; i++)
  {
    REQUIRE(ok(limiter.check_credit(Out{throttled_until}, now)));
    now += std::chrono::milliseconds{1U};
  }

  // We've reached the limit. Token acquisition should fail until enough time
  // passes.
  REQUIRE(fails(limiter.check_credit(Out{throttled_until}, now)));
  const auto first_deadline = now + std::chrono::milliseconds{90};
  REQUIRE(throttled_until == first_deadline);
  now += std::chrono::milliseconds{10U};
  REQUIRE(fails(limiter.check_credit(Out{throttled_until}, now)));
  REQUIRE(throttled_until == first_deadline);

  // At 10 tokens every second, a new tokens should be added every 100ms.
  // Jump forward enough to spawn five of them.
  now += std::chrono::milliseconds{489U};
  for (auto i = 0; i < 5; i++)
  {
    REQUIRE(ok(limiter.check_credit(Out{throttled_until}, now)));
    now += std::chrono::milliseconds{1U};
  }
  REQUIRE(fails(limiter.check_credit(Out{throttled_until}, now)));

  // You shouldn't be able to accumulate more work than the provided limit.
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

} // namespace jewels::rate_limiter
