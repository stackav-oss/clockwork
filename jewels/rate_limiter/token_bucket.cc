// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/rate_limiter/token_bucket.hh"

#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/time/sync_time.hh"

#include <chrono>
#include <compare>
#include <exception>

namespace jewels::rate_limiter
{
namespace
{
std::chrono::nanoseconds validated_cost(const uint64_t limit, const std::chrono::nanoseconds period)
{
  if (limit == 0U || period <= std::chrono::nanoseconds::zero())
  {
    std::terminate();
  }
  const auto cost = std::chrono::nanoseconds(period / limit);
  if (cost <= std::chrono::nanoseconds::zero())
  {
    std::terminate();
  }
  return cost;
}
} // namespace

TokenBucket::TokenBucket(uint64_t limit, std::chrono::nanoseconds period)
  : time_bucket_(period, validated_cost(limit, period))
{
}

BinaryOutcome TokenBucket::check_credit(Out<time::SyncTime> throttled_until_out, const time::SyncTime now)
{
  return time_bucket_.check_credit(Out{*throttled_until_out}, now);
}

} // namespace jewels::rate_limiter
