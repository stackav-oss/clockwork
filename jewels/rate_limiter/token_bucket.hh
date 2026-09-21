// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/rate_limiter/time_bucket.hh"
#include "jewels/time/sync_time.hh"

#include <chrono>
#include <cstdint>

namespace jewels::rate_limiter
{

/// A simple token bucket rate limiter.
/// Construct a TokenBucket with the desired parameters and call check_credit with the current time whenever a token is
/// required. A rejected call provides the first time at which credit will be available.
///
/// A token bucket has two parameters: a limit B and a duration D.
/// One token (AKA a unit of work) is added to the bucket every B/D seconds. If the bucket is already full, the new
/// tokens are simply discarded. Whenever a unit of work is performed a token is removed from the bucket. If the bucket
/// is empty when work arrives, the request to perform work is rejected.
//
/// For more details about how token buckets work see: https://en.wikipedia.org/wiki/Token_bucket
class TokenBucket
{
public:
  /// Construct a TokenBucket.
  /// @param limit How many units of work to add during a period.
  /// @param period The period over which to replenish the bucket.
  explicit TokenBucket(uint64_t limit, std::chrono::nanoseconds period);

  /// Attempt to acquire a token.
  /// @param[out] throttled_until_out The first time at which a token is available on failure.
  /// @param[in] now The current time.
  /// @return Success if a token was acquired, otherwise failure.
  BinaryOutcome check_credit(Out<time::SyncTime> throttled_until_out, time::SyncTime now);

private:
  // Time bucket used to implement the token bucket
  TimeBucket time_bucket_;
};

} // namespace jewels::rate_limiter
