// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/offboard/s3_retry_strategy.hh"
#include "jewels/memory/memory_resource.hh"

#include <aws/core/client/AWSError.h>
#include <aws/core/client/CoreErrors.h>
#include <catch2/catch_test_macros.hpp>

#include <memory_resource>

namespace clockwork_logging::offboard
{
namespace
{

TEST_CASE("S3RetryStrategy")
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const S3RetryStrategy retry_strategy(memory_resource);

  SECTION("Non-retriable error")
  {
    const Aws::Client::AWSError<Aws::Client::CoreErrors> error{Aws::Client::CoreErrors::ACCESS_DENIED, false};
    REQUIRE_FALSE(retry_strategy.ShouldRetry(error, 0));
  }

  SECTION("Retriable error not in the set we try forever")
  {
    const Aws::Client::AWSError<Aws::Client::CoreErrors> error{Aws::Client::CoreErrors::REQUEST_TIME_TOO_SKEWED, true};
    REQUIRE(retry_strategy.ShouldRetry(error, 0));
    REQUIRE(retry_strategy.CalculateDelayBeforeNextRetry(error, 0) == 100);
    REQUIRE(retry_strategy.ShouldRetry(error, 19));
    REQUIRE(retry_strategy.CalculateDelayBeforeNextRetry(error, 19) == 2000);
    REQUIRE_FALSE(retry_strategy.ShouldRetry(error, 20));
  }

  SECTION("Error in the set we try forever")
  {
    const Aws::Client::AWSError<Aws::Client::CoreErrors> error{Aws::Client::CoreErrors::NETWORK_CONNECTION, false};
    REQUIRE(retry_strategy.ShouldRetry(error, 0));
    REQUIRE(retry_strategy.CalculateDelayBeforeNextRetry(error, 0) == 100);
    REQUIRE(retry_strategy.ShouldRetry(error, 25));
    REQUIRE(retry_strategy.CalculateDelayBeforeNextRetry(error, 25) == 2500);
    REQUIRE(retry_strategy.ShouldRetry(error, 50));
    REQUIRE(retry_strategy.CalculateDelayBeforeNextRetry(error, 50) == 2500);
  }
}

} // namespace
} // namespace clockwork_logging::offboard
