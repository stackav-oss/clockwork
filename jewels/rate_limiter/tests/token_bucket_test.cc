// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/rate_limiter/token_bucket.hh"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

namespace jewels::rate_limiter
{

TEST_CASE("TokenBucket")
{
  TokenBucket limiter(10U, time::SyncClock::duration{std::chrono::seconds{1}});
  auto now = time::SyncClock::now();

  // Quickly exhaust the entire pool of tokens.
  for (auto i = 0; i < 10; i++)
  {
    REQUIRE(limiter(now));
    now += std::chrono::milliseconds{1U};
  }

  // We've reached the limit. Token acquisition should fail until enough time
  // passes.
  REQUIRE_FALSE(limiter(now));
  now += std::chrono::milliseconds{10U};
  REQUIRE_FALSE(limiter(now));

  // At 10 tokens every second, a new tokens should be added every 100ms.
  // Jump forward enough to spawn five of them.
  now += std::chrono::milliseconds{489U};
  for (auto i = 0; i < 5; i++)
  {
    REQUIRE(limiter(now));
    now += std::chrono::milliseconds{1U};
  }
  REQUIRE_FALSE(limiter(now));

  // You shouldn't be able to accumulate more work than the provided limit.
  now += std::chrono::minutes{60};
  for (auto i = 0; i < 10; i++)
  {
    REQUIRE(limiter(now));
    now += std::chrono::milliseconds{1U};
  }
  REQUIRE_FALSE(limiter(now));

  // You shouldn't be able to cheat by jumping backwards.
  now -= std::chrono::seconds{10};
  REQUIRE_FALSE(limiter(now));
  now += std::chrono::seconds{10};
  REQUIRE_FALSE(limiter(now));
}

TEST_CASE("Empty Bucket")
{
  /// A bucket with a limit of zero should never grant a token.
  TokenBucket limiter(0U, time::SyncClock::duration{std::chrono::seconds{1}});
  auto now = time::SyncClock::now();
  REQUIRE_FALSE(limiter(now));
  now += std::chrono::years{10};
  REQUIRE_FALSE(limiter(now));
}

} // namespace jewels::rate_limiter
