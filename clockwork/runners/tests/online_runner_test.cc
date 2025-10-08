// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/abstract_cog.hh"
#include "clockwork/common/tests/support/test_cog.hh"
#include "clockwork/runners/online_cog_queue.hh"
#include "clockwork/runners/online_runner.hh"
#include "clockwork/runners/thread_pool.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/time/sync_time.hh"

#include <boost/lockfree/queue.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <thread>
#include <variant>

namespace clockwork
{
namespace
{

TEST_CASE("execute", "[OnlineRunner]")
{
  auto resource = jewels::memory::MemoryResource(std::pmr::new_delete_resource());
  auto queue = OnlineCogQueue(resource);
  auto cog = TestCog(jewels::memory::make_non_null_from_ref(queue));
  auto pool = ThreadPool(
    ThreadPoolConfig{
      .resource = resource,
      .thread_configs = {{.work = jewels::memory::make_non_null_from_ref(queue)}},
    });

  auto config = OnlineRunnerConfig{
    .cogs = {{
      .cog = jewels::memory::make_non_null_from_ref(cog),
    }},
    .pool = jewels::memory::make_non_null_from_ref(pool)};
  auto runner = OnlineRunner(config);

  auto executed_msgs = boost::lockfree::queue<std::optional<TestMsg>>(0);
  auto execute_cb = [&](const CogExecuteParams& /*params*/, const std::optional<TestMsg>& msg)
  { executed_msgs.push(msg); };

  auto msg0 = TestMsg{.value = 0};
  auto msg1 = TestMsg{.value = 1};
  auto msg2 = TestMsg{.value = 2};
  auto msg3 = TestMsg{.value = 3};

  cog.set_execute_callback(execute_cb);
  cog.push(msg0);
  cog.push(msg1);
  cog.push(msg2);
  cog.push(msg3);

  //
  // Start the pool.
  //

  runner.start();

  //
  // Execute the cog.
  //

  auto now = jewels::time::SyncTime{};
  for (const auto& msg : {msg0, msg1, msg2, msg3})
  {
    now += std::chrono::seconds{1};
    cog.signal_is_ready(now);

    while (executed_msgs.empty())
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto executed_msg = std::optional<TestMsg>();
    auto popped = executed_msgs.pop(executed_msg);
    REQUIRE(popped);
    REQUIRE(executed_msg);
    REQUIRE(*executed_msg == msg);
    REQUIRE(executed_msgs.empty());
  }

  runner.stop();
  runner.join();
}

TEST_CASE("ensure mutually exclusive execution shared queue", "[OnlineRunner]")
{
  ///
  /// This test is to ensure that a single non-reentrant cog never runs simultanesously.
  /// We create a system with one cog, two execution threads and one shared work queue.
  /// The cog is always ready.
  ///
  /// 1. Push the cog onto the queue.
  /// 2. Execute the cog, which will remove the cog from the queue and execute it.
  /// 3. The callback will block until the pause flag is unset.
  /// 4. Push the cog onto the queue again.
  /// 5. Ensure that the second instance does not get executed.
  /// 6. Unset the pause flag to allow the first instance to complete.
  /// 6. Ensure the second instance is popped and executed from the queue.
  ///

  auto resource = jewels::memory::MemoryResource(std::pmr::new_delete_resource());
  auto queue = OnlineCogQueue(resource);
  auto cog = TestCog(jewels::memory::make_non_null_from_ref(queue));
  auto pool = ThreadPool(
    ThreadPoolConfig{
      .resource = resource,
      .thread_configs =
        {{.work = jewels::memory::make_non_null_from_ref(queue)},
         {.work = jewels::memory::make_non_null_from_ref(queue)}},
    });

  auto config = OnlineRunnerConfig{
    .cogs = {{
      .cog = jewels::memory::make_non_null_from_ref(cog),
    }},
    .pool = jewels::memory::make_non_null_from_ref(pool)};
  auto runner = OnlineRunner(config);

  auto msg0 = TestMsg{.value = 0};
  auto msg1 = TestMsg{.value = 1};

  std::mutex pause_mutex;
  std::condition_variable pause_cv;

  bool pause_execution = true;
  std::array<int64_t, 2> execution_start{};
  std::array<int64_t, 2> execution_end{};

  std::mutex start_mutex;
  std::condition_variable start_cv;

  auto execute_cb = [&](const CogExecuteParams& /*params*/, const std::optional<TestMsg>& msg)
  {
    {
      const std::unique_lock start_lock(start_mutex);
      execution_start.at(static_cast<size_t>(msg->value)) = std::chrono::steady_clock::now().time_since_epoch().count();
    }
    start_cv.notify_one();

    std::unique_lock pause_lock(pause_mutex);
    pause_cv.wait(pause_lock, [&] { return !pause_execution; });
    execution_end.at(static_cast<size_t>(msg->value)) = std::chrono::steady_clock::now().time_since_epoch().count();
  };

  cog.set_execute_callback(execute_cb);
  cog.push(msg0);
  cog.push(msg1);

  //
  // Start the pool.
  //

  runner.start();

  //
  // Notify that the cog is ready once.
  //

  auto now = jewels::time::SyncTime{};
  cog.signal_is_ready(now);

  //
  // Wait until its executing.
  //

  {
    std::unique_lock lock(start_mutex);
    start_cv.wait(lock, [&] { return execution_start.at(0) > 0; });
  }

  //
  // Notify that the cog is ready again.
  //

  cog.signal_is_ready(now);

  //
  // Wait for some time, the second cog should not run until the first is done
  // so this is an arbitrary amount of time to cover any cpu load causing it not to run.
  // This is non-deterministic way to try and catch errors/race conditions.
  //

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  REQUIRE(execution_start.at(1) == 0);

  //
  // Allow the executions to run.
  //

  {
    const std::unique_lock lock(pause_mutex);
    pause_execution = false;
  }
  pause_cv.notify_one();

  //
  // Wait until the second instance is executing.
  //

  {
    std::unique_lock lock(start_mutex);
    start_cv.wait(lock, [&] { return execution_start.at(1) > 0; });
  }

  //
  // Stop the runner.
  //

  runner.stop();
  runner.join();

  //
  // Ensure both instances ran and the second one ran after the first ended.
  //

  REQUIRE(execution_start[0] > 0);
  REQUIRE(execution_end[0] >= execution_start[0]);

  REQUIRE(execution_start[1] > 0);
  REQUIRE(execution_end[1] >= execution_start[1]);

  REQUIRE(execution_start[1] >= execution_end[0]);
}

TEST_CASE("simulate shared state wake up", "[OnlineRunner]")
{
  ///
  /// This test is to ensure that the execution queues re-check existing cogs for readiness after
  /// any cog completes execution. This is to ensure that if cogs that share state are executed after
  /// the shared state is made available.
  ///
  /// We create a system with two cogs, two execution thread and one shared work queue.
  /// The cogs share a mutex which will cause their prepare function to fail if the other holds the lock.
  ///
  /// 1. Push both cogs onto the queue.
  /// 2. Start the runner.
  /// 3. One cog will acquire the lock and start executing.
  /// 4. Ensure that one and only one is executing.
  /// 5. Unset the pause flag to allow the cog to complete.
  /// 6. The running cog should complete execution and notify the queue its done.
  /// 7. The queue will re-process its elements and execute the second cog now that the shared mutex is not locked.
  ///

  auto resource = jewels::memory::MemoryResource(std::pmr::new_delete_resource());
  auto queue = OnlineCogQueue(resource);
  auto shared_state_mutex = std::make_shared<std::shared_mutex>();
  auto cog0 = TestCog(jewels::memory::make_non_null_from_ref(queue), shared_state_mutex);
  auto cog1 = TestCog(jewels::memory::make_non_null_from_ref(queue), shared_state_mutex);
  auto pool = ThreadPool(
    ThreadPoolConfig{
      .resource = resource,
      .thread_configs =
        {
          {.work = jewels::memory::make_non_null_from_ref(queue)},
          {.work = jewels::memory::make_non_null_from_ref(queue)},
        },
    });

  auto config = OnlineRunnerConfig{
    .cogs =
      {{
         .cog = jewels::memory::make_non_null_from_ref(cog0),
       },
       {
         .cog = jewels::memory::make_non_null_from_ref(cog1),
       }},
    .pool = jewels::memory::make_non_null_from_ref(pool)};
  auto runner = OnlineRunner(config);

  auto msg0 = TestMsg{.value = 0};
  auto msg1 = TestMsg{.value = 1};

  std::mutex pause_mutex;
  std::condition_variable pause_cv;

  bool pause_execution = true;
  std::array<int64_t, 2> execution_start{};
  std::array<int64_t, 2> execution_end{};

  std::mutex start_mutex;
  std::condition_variable start_cv;

  auto execute_cb = [&](const CogExecuteParams& /*params*/, const std::optional<TestMsg>& msg)
  {
    {
      const std::unique_lock start_lock(start_mutex);
      execution_start.at(static_cast<size_t>(msg->value)) = std::chrono::steady_clock::now().time_since_epoch().count();
    }
    start_cv.notify_one();

    std::unique_lock pause_lock(pause_mutex);
    pause_cv.wait(pause_lock, [&] { return !pause_execution; });
    execution_end.at(static_cast<size_t>(msg->value)) = std::chrono::steady_clock::now().time_since_epoch().count();
  };

  cog0.set_execute_callback(execute_cb);
  cog0.push(msg0);
  cog1.set_execute_callback(execute_cb);
  cog1.push(msg1);

  //
  // Start the pool.
  //

  runner.start();

  //
  // Notify that the cog is ready once.
  //

  auto now = jewels::time::SyncTime{};
  cog0.signal_is_ready(now);
  cog1.signal_is_ready(now);

  //
  // Wait until its executing.
  //

  {
    std::unique_lock lock(start_mutex);
    start_cv.wait(lock, [&] { return std::ranges::any_of(execution_start, [](const auto& val) { return val > 0; }); });
  }

  //
  // Wait for some time, the second cog should not run until the first is done
  // so this is an arbitrary amount of time to cover any cpu load causing it not to run.
  // This is non-deterministic way to try and catch errors/race conditions.
  //

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  REQUIRE(std::ranges::any_of(execution_start, [](const auto& val) { return val == 0; }));

  //
  // Allow the executions to run.
  //

  {
    const std::unique_lock lock(pause_mutex);
    pause_execution = false;
  }
  pause_cv.notify_one();

  //
  // Wait until the second instance is executing.
  //

  {
    std::unique_lock lock(start_mutex);
    start_cv.wait(lock, [&] { return std::ranges::all_of(execution_start, [](const auto& val) { return val > 0; }); });
  }

  //
  // Stop the runner.
  //

  runner.stop();
  runner.join();

  //
  // Ensure both instances ran.
  //

  REQUIRE(std::ranges::all_of(execution_start, [](const auto& val) { return val > 0; }));
}

} // namespace
} // namespace clockwork
