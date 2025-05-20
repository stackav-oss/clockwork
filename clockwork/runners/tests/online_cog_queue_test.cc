// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/abstract_cog_queue.hh"
#include "clockwork/common/cog_envelope.hh"
#include "clockwork/common/tests/support/test_cog.hh"
#include "clockwork/runners/online_cog_queue.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/time/sync_time.hh"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <compare>
#include <memory_resource>

namespace clockwork
{
namespace
{

TEST_CASE("push and pop", "OnlineCogQueue")
{
  auto queue = OnlineCogQueue(jewels::memory::MemoryResource(std::pmr::new_delete_resource()));

  auto cog0 = TestCog(jewels::memory::make_non_null_from_ref(queue));
  auto env0 = CogEnvelope{
    .ready_time = jewels::time::SyncTime(std::chrono::seconds{1}),
    .cog = jewels::memory::make_non_null_from_ref(cog0),
  };

  auto cog1 = TestCog(jewels::memory::make_non_null_from_ref(queue));
  auto env1 = CogEnvelope{
    .ready_time = jewels::time::SyncTime(std::chrono::seconds{1}),
    .cog = jewels::memory::make_non_null_from_ref(cog1),
  };

  SECTION("pop unexpected when queue is empty")
  {
    auto stats = queue.stats();
    REQUIRE(0 == stats.size);
    REQUIRE_FALSE(queue.pop({}));
  }

  SECTION("wait at least timeout")
  {
    auto timeout = std::chrono::milliseconds(50);
    auto time0 = std::chrono::steady_clock::now();
    REQUIRE_FALSE(queue.pop(timeout));
    auto time1 = std::chrono::steady_clock::now();
    REQUIRE((time1 - time0) >= timeout);
  }

  SECTION("once")
  {
    queue.push(env0);
    auto stats = queue.stats();
    REQUIRE(1 == stats.size);

    auto result = queue.pop({});
    REQUIRE(result);
    REQUIRE(env0 == *result);

    stats = queue.stats();
    REQUIRE(0 == stats.size);
  }

  SECTION("multiple")
  {
    queue.push(env0);
    queue.push(env1);
    auto stats = queue.stats();
    REQUIRE(2 == stats.size);

    auto result = queue.pop({});
    REQUIRE(result);
    REQUIRE(env0 == *result);

    stats = queue.stats();
    REQUIRE(1 == stats.size);

    result = queue.pop({});
    REQUIRE(result);
    REQUIRE(env1 == *result);

    stats = queue.stats();
    REQUIRE(0 == stats.size);

    result = queue.pop({});
    REQUIRE_FALSE(result);
  }

  SECTION("encforce only one instace per cog")
  {
    auto env2 = env0;
    env2.ready_time += std::chrono::seconds(1);

    queue.push(env0);
    queue.push(env1);
    auto stats = queue.stats();
    REQUIRE(2 == stats.size);

    queue.push(env0);
    stats = queue.stats();
    REQUIRE(2 == stats.size);

    queue.push(env2);
    stats = queue.stats();
    REQUIRE(2 == stats.size);

    auto result = queue.pop({});
    REQUIRE(result);
    REQUIRE(env0 == *result);
    result = queue.pop({});
    REQUIRE(result);
    REQUIRE(env1 == *result);
    REQUIRE_FALSE(queue.pop({}));
  }
}

} // namespace
} // namespace clockwork
