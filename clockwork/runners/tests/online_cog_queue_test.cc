// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/abstract_cog.hh"
#include "clockwork/common/abstract_cog_queue.hh"
#include "clockwork/common/cog_envelope.hh"
#include "clockwork/common/cog_execution_error_clk_cc.hh"
#include "clockwork/common/tests/support/test_cog.hh"
#include "clockwork/runners/online_cog_queue.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <compare>
#include <memory_resource>
#include <string_view>

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

///
/// Test cog that always returns not_ready on prepare_for_execution
///
class NotReadyCog : public AbstractCog
{
public:
  explicit NotReadyCog(jewels::memory::ObjectPtr<AbstractCogQueue> queue)
    : AbstractCog(queue)
  {
  }

  [[nodiscard]] std::string_view get_name() const override
  {
    return "clockwork::NotReadyCog";
  }

  jewels::expected<void, jewels::MonoError> prime(jewels::time::SyncTime /*start_time*/) override
  {
    return {};
  }

  jewels::expected<void, CogExecutionError> prepare_for_execution(jewels::time::SyncTime /*current_time*/) override
  {
    return jewels::unexpected(CogExecutionError::not_ready);
  }

  jewels::expected<void, CogExecutionError> execute(CogExecuteParams /*params*/) override
  {
    return {};
  }
};

TEST_CASE("not_ready cogs are removed from queue", "OnlineCogQueue")
{
  auto queue = OnlineCogQueue(jewels::memory::MemoryResource(std::pmr::new_delete_resource()));

  // Create a mix of regular cogs and not_ready cogs
  auto ready_cog = TestCog(jewels::memory::make_non_null_from_ref(queue));
  auto not_ready_cog1 = NotReadyCog(jewels::memory::make_non_null_from_ref(queue));
  auto not_ready_cog2 = NotReadyCog(jewels::memory::make_non_null_from_ref(queue));

  auto ready_env = CogEnvelope{
    .ready_time = jewels::time::SyncTime(std::chrono::seconds{1}),
    .cog = jewels::memory::make_non_null_from_ref(ready_cog),
  };

  auto not_ready_env1 = CogEnvelope{
    .ready_time = jewels::time::SyncTime(std::chrono::seconds{1}),
    .cog = jewels::memory::make_non_null_from_ref(not_ready_cog1),
  };

  auto not_ready_env2 = CogEnvelope{
    .ready_time = jewels::time::SyncTime(std::chrono::seconds{1}),
    .cog = jewels::memory::make_non_null_from_ref(not_ready_cog2),
  };

  SECTION("multiple consecutive not_ready cogs before ready cog")
  {
    // This tests the fix: push multiple consecutive not_ready cogs
    // before a ready cog, ensuring the loop can safely continue after
    // erasing each not_ready cog
    queue.push(not_ready_env1);
    queue.push(not_ready_env2);
    queue.push(ready_env);

    auto stats = queue.stats();
    REQUIRE(3 == stats.size);

    // Pop should skip over both not_ready cogs (removing them) and return the ready one
    auto result = queue.pop({});
    REQUIRE(result);
    REQUIRE(ready_env == *result);

    // All cogs should have been processed/removed
    stats = queue.stats();
    REQUIRE(0 == stats.size);
  }

  SECTION("all not_ready cogs are removed when no ready cog exists")
  {
    // Push only not_ready cogs
    queue.push(not_ready_env1);
    queue.push(not_ready_env2);

    auto stats = queue.stats();
    REQUIRE(2 == stats.size);

    // Pop should return nothing and remove all not_ready cogs
    auto result = queue.pop({});
    REQUIRE_FALSE(result);

    // Queue should be empty - all not_ready cogs removed
    stats = queue.stats();
    REQUIRE(0 == stats.size);
  }

  SECTION("not_ready cog in middle of queue")
  {
    // Test with not_ready cog between two ready cogs
    auto ready_cog2 = TestCog(jewels::memory::make_non_null_from_ref(queue));
    auto ready_env2 = CogEnvelope{
      .ready_time = jewels::time::SyncTime(std::chrono::seconds{1}),
      .cog = jewels::memory::make_non_null_from_ref(ready_cog2),
    };

    queue.push(ready_env);
    queue.push(not_ready_env1);
    queue.push(ready_env2);

    auto stats = queue.stats();
    REQUIRE(3 == stats.size);

    // First pop returns first ready cog
    auto result = queue.pop({});
    REQUIRE(result);
    REQUIRE(ready_env == *result);

    // After first pop, not_ready cog should still be there
    // (we returned early when we found the first ready cog)
    stats = queue.stats();
    REQUIRE(2 == stats.size);

    // Second pop should skip the not_ready cog and return the second ready cog
    result = queue.pop({});
    REQUIRE(result);
    REQUIRE(ready_env2 == *result);

    // Now queue should be empty
    stats = queue.stats();
    REQUIRE(0 == stats.size);
  }
}

} // namespace
} // namespace clockwork
