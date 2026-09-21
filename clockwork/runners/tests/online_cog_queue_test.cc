// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/abstract_cog.hh"
#include "clockwork/common/abstract_cog_queue.hh"
#include "clockwork/common/cog_envelope.hh"
#include "clockwork/common/tests/support/test_cog.hh"
#include "clockwork/runners/online_cog_queue.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <chrono>
#include <compare>
#include <cstddef>
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

  CogPrepareOutcome prepare_for_execution(
    jewels::Out<jewels::time::SyncTime> /*throttled_until_out*/, jewels::time::SyncTime /*current_time*/) override
  {
    return CogPrepareResult::not_ready;
  }

  jewels::expected<void, CogExecutionError> execute(CogExecuteParams /*params*/) override
  {
    return {};
  }
};

/// Test Cog that is publisher-throttled until a fixed deadline and ready afterward.
class PublisherThrottledCog : public AbstractCog
{
public:
  PublisherThrottledCog(jewels::memory::ObjectPtr<AbstractCogQueue> queue, const jewels::time::SyncTime throttled_until)
    : AbstractCog(queue), throttled_until_(throttled_until)
  {
  }

  [[nodiscard]] std::string_view get_name() const override
  {
    return "clockwork::PublisherThrottledCog";
  }

  jewels::expected<void, jewels::MonoError> prime(jewels::time::SyncTime /*start_time*/) override
  {
    return {};
  }

  CogPrepareOutcome prepare_for_execution(
    jewels::Out<jewels::time::SyncTime> throttled_until_out, const jewels::time::SyncTime current_time) override
  {
    ++prepare_count_;
    if (current_time < throttled_until_)
    {
      *throttled_until_out = throttled_until_;
      return CogPrepareResult::publisher_throttled;
    }
    return CogPrepareResult::ready;
  }

  jewels::expected<void, CogExecutionError> execute(CogExecuteParams /*params*/) override
  {
    return {};
  }

  [[nodiscard]] size_t prepare_count() const
  {
    return prepare_count_;
  }

private:
  jewels::time::SyncTime throttled_until_;
  size_t prepare_count_{};
};

/// Test Cog that reports one lock-contention outcome before becoming ready.
class ContendedCog : public AbstractCog
{
public:
  ContendedCog(jewels::memory::ObjectPtr<AbstractCogQueue> queue, const CogPrepareResult contention_result)
    : AbstractCog(queue), contention_result_(contention_result)
  {
  }

  [[nodiscard]] std::string_view get_name() const override
  {
    return "clockwork::ContendedCog";
  }

  jewels::expected<void, jewels::MonoError> prime(jewels::time::SyncTime /*start_time*/) override
  {
    return {};
  }

  CogPrepareOutcome prepare_for_execution(
    jewels::Out<jewels::time::SyncTime> /*throttled_until_out*/, jewels::time::SyncTime /*current_time*/) override
  {
    if (!reported_contention_)
    {
      reported_contention_ = true;
      return contention_result_;
    }
    return CogPrepareResult::ready;
  }

  jewels::expected<void, CogExecutionError> execute(CogExecuteParams /*params*/) override
  {
    return {};
  }

private:
  CogPrepareResult contention_result_;
  bool reported_contention_{};
};

TEST_CASE("lock contention remains distinct and retains the queue node", "OnlineCogQueue")
{
  auto queue = OnlineCogQueue(jewels::memory::MemoryResource(std::pmr::new_delete_resource()));
  const auto contention_result =
    GENERATE(CogPrepareResult::states_lock_contention, CogPrepareResult::reentry_lock_contention);
  auto cog = ContendedCog(jewels::memory::make_non_null_from_ref(queue), contention_result);
  const auto envelope = CogEnvelope{
    .ready_time = jewels::time::SyncTime{std::chrono::seconds{1}},
    .cog = jewels::memory::make_non_null_from_ref(cog),
  };
  queue.push(envelope);

  CHECK_FALSE(queue.pop(std::chrono::nanoseconds::zero()));
  CHECK(queue.stats().size == 1);
  const auto result = queue.pop(std::chrono::nanoseconds::zero());
  REQUIRE(result);
  CHECK(result->cog == envelope.cog);
  CHECK(queue.stats().size == 0);
}

TEST_CASE("publisher-throttled Cogs retain FIFO position and make deadline progress", "OnlineCogQueue")
{
  using namespace std::chrono_literals;
  auto queue = OnlineCogQueue(jewels::memory::MemoryResource(std::pmr::new_delete_resource()));
  const auto deadline = jewels::time::SyncClock::now() + 40ms;
  auto throttled_cog = PublisherThrottledCog(jewels::memory::make_non_null_from_ref(queue), deadline);
  auto ready_cog = TestCog(jewels::memory::make_non_null_from_ref(queue));
  const auto throttled_env = CogEnvelope{
    .ready_time = deadline - 1s,
    .cog = jewels::memory::make_non_null_from_ref(throttled_cog),
  };
  const auto ready_env = CogEnvelope{
    .ready_time = deadline - 500ms,
    .cog = jewels::memory::make_non_null_from_ref(ready_cog),
  };
  queue.push(throttled_env);
  queue.push(ready_env);

  // A later runnable Cog progresses around the retained throttled FIFO node.
  const auto later_result = queue.pop(0ns);
  REQUIRE(later_result);
  CHECK(later_result->cog == ready_env.cog);
  CHECK(queue.stats().size == 1);
  CHECK(throttled_cog.prepare_count() == 1);

  // Duplicate and stale notifications do not prepare the Cog before its deadline.
  queue.notify();
  CHECK_FALSE(queue.pop(0ns));
  CHECK(throttled_cog.prepare_count() == 1);

  // The queue's timed-wait fallback wakes at the deadline even without a working private timer.
  const auto wait_start = jewels::time::SyncClock::now();
  const auto throttled_result = queue.pop(200ms);
  const auto execution_time = jewels::time::SyncClock::now();
  REQUIRE(throttled_result);
  CHECK(throttled_result->cog == throttled_env.cog);
  CHECK(execution_time >= deadline);
  CHECK(execution_time - wait_start < 200ms);
  CHECK(throttled_cog.prepare_count() == 2);
  CHECK(queue.stats().size == 0);
}

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
