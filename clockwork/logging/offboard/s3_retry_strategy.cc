// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/offboard/s3_retry_strategy.hh"

#include "jewels/log_cerr/log_cerr.hh"

#include <aws/core/client/AWSError.h>

#include <algorithm>
#include <array>
#include <string>

namespace clockwork_logging::offboard
{

namespace
{

constexpr std::array inifinite_retry_errors{
  Aws::Client::CoreErrors::THROTTLING,
  Aws::Client::CoreErrors::SLOW_DOWN,
  Aws::Client::CoreErrors::NETWORK_CONNECTION,
};

/// Number of retries between retry debug messages
constexpr int64_t num_retries_between_debug_messages = 10;

/// Initial retry delay in milliseconds
constexpr int64_t initial_retry_delay_ms = 100;

/// Maximum retry delay multiplier
constexpr int64_t max_retry_delay_multiplier = 25;

/// Default number of retries for retriable errors
constexpr int64_t default_max_retries = 20;

} // namespace

S3RetryStrategy::S3RetryStrategy(jewels::memory::MemoryResource memory_resource)
  : infinite_retry_errors_(memory_resource)
{
  infinite_retry_errors_.insert(inifinite_retry_errors.begin(), inifinite_retry_errors.end());
}

bool S3RetryStrategy::ShouldRetry(
  const Aws::Client::AWSError<Aws::Client::CoreErrors>& error, int64_t attempted_retries) const
{
  const bool should_retry = infinite_retry_errors_.contains(error.GetErrorType()) ||
                            (error.ShouldRetry() && attempted_retries < default_max_retries);
  if (should_retry && (attempted_retries != 0) && (attempted_retries % num_retries_between_debug_messages == 0))
  {
    jewels::log_cerr_info("Retrying {}: attempted_retries {}", error.GetMessage(), attempted_retries);
  }
  return should_retry;
}

int64_t S3RetryStrategy::CalculateDelayBeforeNextRetry(
  const Aws::Client::AWSError<Aws::Client::CoreErrors>& /*error*/, int64_t attempted_retries) const
{
  return initial_retry_delay_ms * std::min(attempted_retries + 1, max_retry_delay_multiplier);
}

} // namespace clockwork_logging::offboard
