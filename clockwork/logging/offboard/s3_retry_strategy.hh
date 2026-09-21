// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/memory/memory_resource.hh"

#include <aws/core/client/CoreErrors.h>
#include <aws/core/client/RetryStrategy.h>

#include <cstdint>
#include <functional>
#include <memory_resource>
#include <unordered_set>

namespace clockwork_logging::offboard
{

/// S3 retry strategy for offboard logs
/// @tparam S3UtilsType S3 utility helper type
template <typename S3UtilsType>
class S3RetryStrategy : public Aws::Client::RetryStrategy
{
public:
  /// S3 errors that we retry indefinitely
  static constexpr std::array inifinite_retry_errors{
    Aws::Client::CoreErrors::THROTTLING,
    Aws::Client::CoreErrors::SLOW_DOWN,
    Aws::Client::CoreErrors::NETWORK_CONNECTION,
  };

  /// Default number of retries for retriable errors
  static constexpr int64_t default_max_retries = 20;

  /// Initial retry delay in milliseconds
  static constexpr int64_t initial_retry_delay_ms = 100;

  /// Maximum retry delay multiplier
  static constexpr int64_t max_retry_delay_multiplier = 25;

  explicit S3RetryStrategy(jewels::memory::MemoryResource memory_resource);

  ~S3RetryStrategy() override = default;

  S3RetryStrategy(const S3RetryStrategy& other) = delete;
  S3RetryStrategy& operator=(const S3RetryStrategy& other) = delete;
  S3RetryStrategy(S3RetryStrategy&&) noexcept = default;
  S3RetryStrategy& operator=(S3RetryStrategy&&) noexcept = default;

  /// Test whether a request should be retried
  /// @param[in] error AWS error
  /// @param[in] attempted_retries Number of attempted retries
  /// @return True if the request should be retried
  [[nodiscard]] bool
  ShouldRetry(const Aws::Client::AWSError<Aws::Client::CoreErrors>& error, int64_t attempted_retries) const override;

  /// Calculate the delay in milliseconds before the next retry
  /// @param[in] error AWS error
  /// @param[in] attempted_retries Number of attempted retries
  /// @return Number of milliseconds to delay before the next retry
  [[nodiscard]] int64_t CalculateDelayBeforeNextRetry(
    const Aws::Client::AWSError<Aws::Client::CoreErrors>& error, int64_t attempted_retries) const override;

private:
  /// Set of errors that we want to retry forever because the request should eventually succeed
  std::pmr::unordered_set<Aws::Client::CoreErrors> infinite_retry_errors_;
};

} // namespace clockwork_logging::offboard

#include "clockwork/logging/offboard/s3_retry_strategy.inl"
