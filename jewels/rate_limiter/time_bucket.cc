// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/rate_limiter/time_bucket.hh"

#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/time/sync_time.hh"

#include <algorithm>
#include <chrono>
#include <compare>
#include <exception>

namespace jewels::rate_limiter
{

TimeBucket::TimeBucket(const std::chrono::nanoseconds limit, const std::chrono::nanoseconds cost)
  : limit_(limit), credits_(limit), cost_(cost)
{
  if (limit <= std::chrono::nanoseconds::zero() || cost <= std::chrono::nanoseconds::zero() || cost > limit)
  {
    std::terminate();
  }
}

BinaryOutcome TimeBucket::check_credit(Out<time::SyncTime> throttled_until_out, const time::SyncTime now)
{
  if (now > last_credit_check_)
  {
    const auto new_credits = now - last_credit_check_;
    last_credit_check_ = now;
    credits_ = std::min(limit_, credits_ + new_credits);
  }
  if (credits_ < cost_)
  {
    *throttled_until_out = last_credit_check_ + (cost_ - credits_);
    return failure;
  }
  credits_ -= cost_;
  return success;
}

ThreadSafeTimeBucket::ThreadSafeTimeBucket(std::chrono::nanoseconds limit, std::chrono::nanoseconds cost)
  : time_bucket_(limit, cost)
{
}

BinaryOutcome ThreadSafeTimeBucket::check_credit(Out<time::SyncTime> throttled_until_out, const time::SyncTime now)
{
  std::lock_guard guard{mutex_};
  return time_bucket_.check_credit(Out{*throttled_until_out}, now);
}

} // namespace jewels::rate_limiter
