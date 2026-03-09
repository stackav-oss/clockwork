// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

// IWYU pragma: private, include "clockwork/logging/offboard/s3_retry_strategy.hh"

#pragma once

#include "clockwork/logging/offboard/s3_retry_strategy.hh"

#include "jewels/memory/memory_resource.hh"

#include <aws/core/client/AWSError.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <memory_resource>

namespace clockwork_logging::offboard
{

template <typename S3UtilsType>
S3RetryStrategy<S3UtilsType>::S3RetryStrategy(jewels::memory::MemoryResource memory_resource)
  : infinite_retry_errors_(memory_resource)
{
  infinite_retry_errors_.insert(inifinite_retry_errors.begin(), inifinite_retry_errors.end());
}

template <typename S3UtilsType>
bool S3RetryStrategy<S3UtilsType>::ShouldRetry(
  const Aws::Client::AWSError<Aws::Client::CoreErrors>& error, int64_t attempted_retries) const
{
  const bool should_retry = infinite_retry_errors_.contains(error.GetErrorType()) ||
                            (error.ShouldRetry() && attempted_retries < default_max_retries);
  S3UtilsType::retry_callback(error, attempted_retries, should_retry);
  return should_retry;
}

template <typename S3UtilsType>
int64_t S3RetryStrategy<S3UtilsType>::CalculateDelayBeforeNextRetry(
  const Aws::Client::AWSError<Aws::Client::CoreErrors>& /*error*/, int64_t attempted_retries) const
{
  return initial_retry_delay_ms * std::min(attempted_retries + 1, max_retry_delay_multiplier);
}

} // namespace clockwork_logging::offboard
