// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/rate_limiter/token_bucket.hh"

#include "jewels/time/sync_time.hh"

#include <algorithm>
#include <chrono>
#include <optional>
#include <ratio>

namespace jewels::rate_limiter
{

TokenBucket::TokenBucket(uint64_t limit, time::SyncClock::duration period)
  : limit_{limit}, period_ns_{std::chrono::duration_cast<std::chrono::nanoseconds>(period)}, available_tokens_{limit}
{
}

bool TokenBucket::operator()(time::SyncTime now)
{
  if (!last_token_)
  {
    last_token_ = now;
  }

  auto elapsed = std::chrono::duration_cast<std::chrono::duration<double, std::chrono::nanoseconds::period>>(
    std::max(now, *last_token_) - *last_token_);
  auto new_tokens = static_cast<uint64_t>(static_cast<double>(limit_) * elapsed / period_ns_);
  available_tokens_ = std::min(available_tokens_ + new_tokens, limit_);
  if (available_tokens_ == 0)
  {
    return false;
  }

  last_token_ = now;
  available_tokens_--;
  return true;
}

} // namespace jewels::rate_limiter
