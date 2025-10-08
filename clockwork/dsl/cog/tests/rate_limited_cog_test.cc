// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dsl/cog/tests/support/rate_limited_cog.hh"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

namespace clockwork
{
TEST_CASE("Rate Limit Parameters")
{
  STATIC_CHECK(testing::RateLimitedCogPolicy::OutputAPolicy::rate_limit_params);
  STATIC_CHECK(testing::RateLimitedCogPolicy::OutputAPolicy::rate_limit_params->limit == 1U);
  STATIC_CHECK(
    std::chrono::duration_cast<std::chrono::seconds>(
      testing::RateLimitedCogPolicy::OutputAPolicy::rate_limit_params->period) == std::chrono::seconds{1U});

  STATIC_CHECK(testing::RateLimitedCogPolicy::OutputBPolicy::rate_limit_params);
  STATIC_CHECK(testing::RateLimitedCogPolicy::OutputBPolicy::rate_limit_params->limit == 3U);
  STATIC_CHECK(
    std::chrono::duration_cast<std::chrono::milliseconds>(
      testing::RateLimitedCogPolicy::OutputBPolicy::rate_limit_params->period) == std::chrono::milliseconds{15U});

  STATIC_CHECK_FALSE(testing::RateLimitedCogPolicy::OutputCPolicy::rate_limit_params);
}
} // namespace clockwork
