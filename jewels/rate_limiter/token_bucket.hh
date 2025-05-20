// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/time/sync_time.hh"

#include <chrono>
#include <cstdint>
#include <optional>

namespace jewels::rate_limiter
{

/// A simple token bucket rate limiter.
/// The interface is very simple. Just construct a TokenBucket with your desired parameters and call it with the current
/// time. If you haven't exceeded the limit, the call will return true, otherwise it will return false you'll have to
/// wait.
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
  explicit TokenBucket(uint64_t limit, time::SyncClock::duration period);

  /// Attempt to acquire a token.
  /// @param now The current time.
  /// @return false if the limit has been exceeded.
  bool operator()(time::SyncTime now);

private:
  // The number of tokens to add every period.
  uint64_t limit_;
  // The length of the period.
  std::chrono::duration<double, std::chrono::nanoseconds::period> period_ns_;
  // The number of work units remaining in the current interval.
  uint64_t available_tokens_{0U};
  // Timestamp of the last time we successfully issued a token.
  std::optional<time::SyncTime> last_token_{};
};

} // namespace jewels::rate_limiter
