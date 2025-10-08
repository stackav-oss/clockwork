// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/offboard/async_work_queue.hh"

#include <catch2/catch_test_macros.hpp>

#include <condition_variable>
#include <cstddef>
#include <mutex>

namespace clockwork_logging::offboard
{
namespace
{

TEST_CASE("AsyncWorQueue")
{
  AsyncWorkQueue async_work_queue{1U};
  std::mutex mutex;
  std::condition_variable condvar;
  size_t test_counter{0U};

  SECTION("Schedule work item")
  {
    async_work_queue.schedule_work_item(
      [&mutex, &condvar, &test_counter]()
      {
        const std::scoped_lock guard{mutex};
        ++test_counter;
        condvar.notify_one();
      });
    async_work_queue.schedule_work_item(
      [&mutex, &condvar, &test_counter]()
      {
        const std::scoped_lock guard{mutex};
        ++test_counter;
        condvar.notify_one();
      });
    std::unique_lock ulock{mutex};
    condvar.wait(ulock, [&test_counter] { return test_counter == 2U; });
    REQUIRE(test_counter == 2U);
  }

  SECTION("Schedule work item no wait")
  {
    async_work_queue.schedule_work_item_no_wait(
      [&mutex, &condvar, &test_counter]()
      {
        const std::scoped_lock guard{mutex};
        ++test_counter;
        condvar.notify_one();
      });
    async_work_queue.schedule_work_item_no_wait(
      [&mutex, &condvar, &test_counter]()
      {
        const std::scoped_lock guard{mutex};
        ++test_counter;
        condvar.notify_one();
      });
    std::unique_lock ulock{mutex};
    condvar.wait(ulock, [&test_counter] { return test_counter == 2U; });
    REQUIRE(test_counter == 2U);
  }
}

} // namespace
} // namespace clockwork_logging::offboard
