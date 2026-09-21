// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/time/sync_time.hh"

#include <chrono>
#include <mutex>

namespace jewels::rate_limiter
{

/// A simple time bucket rate limiter.
/// The interface is very simple. Just construct a TimeBucket with your desired parameters and call it with the current
/// time. If you haven't exceeded the limit, the call will return true, otherwise it will return false you'll have to
/// wait.
///
/// A time bucket has two parameters, an intial/maximum credit limit, and a cost for using the bucket.
/// One credit is added to the bucket for each nanosecond of time that elapses up to the maximum credit limit.
/// Whenever a unit of work is performed, the cost is removed from the bucket. If the bucket doesn't contain
/// enought credits then the request to perform work is rejected.
class TimeBucket
{
public:
  /// Construct a TimeBucket.
  /// @param limit Initial/maximum number of credits
  /// @param cost Cost per unit of work
  TimeBucket(std::chrono::nanoseconds limit, std::chrono::nanoseconds cost);

  /// Check if a unit of work is available in the bucket, deducting the cost from the credits if the work can proceed.
  /// @param[out] throttled_until_out The first time at which enough credit is available on failure.
  /// @param[in] now The current time.
  /// @return Success if the work can proceed, otherwise failure.
  BinaryOutcome check_credit(Out<time::SyncTime> throttled_until_out, time::SyncTime now);

private:
  // The initial/maximum number of credits
  std::chrono::nanoseconds limit_;
  // The current number of credits
  std::chrono::nanoseconds credits_;
  // The cost per unit of work
  std::chrono::nanoseconds cost_;
  // Time of the last credit check
  time::SyncTime last_credit_check_{};
};

/// A thread safe wrapper around the TimeBucket class
class ThreadSafeTimeBucket
{
public:
  /// Construct a TimeBucket.
  /// @param limit Initial/maximum number of credits
  /// @param cost Cost per unit of work
  ThreadSafeTimeBucket(std::chrono::nanoseconds limit, std::chrono::nanoseconds cost);

  /// Check if a unit of work is available in the bucket, deducting the cost from the credits if the work can proceed.
  /// @param[out] throttled_until_out The first time at which enough credit is available on failure.
  /// @param[in] now The current time.
  /// @return Success if the work can proceed, otherwise failure.
  BinaryOutcome check_credit(Out<time::SyncTime> throttled_until_out, time::SyncTime now);

private:
  // The wrapped time bucket
  TimeBucket time_bucket_;
  // Mutex to serialize access to the time bucket
  std::mutex mutex_;
};

} // namespace jewels::rate_limiter
