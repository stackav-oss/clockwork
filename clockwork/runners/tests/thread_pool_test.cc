// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/abstract_cog.hh"
#include "clockwork/common/abstract_epoll_manager.hh"
#include "clockwork/common/cog_envelope.hh"
#include "clockwork/common/tests/support/test_cog.hh"
#include "clockwork/runners/epoll_manager.hh"
#include "clockwork/runners/online_cog_queue.hh"
#include "clockwork/runners/thread_pool.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_shared_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/time/sync_time.hh"

#include <boost/lockfree/queue.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <memory_resource>
#include <optional>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <thread>
#include <unistd.h>
#include <variant>

namespace clockwork
{
namespace
{

void signal_eventfd(jewels::filesystem::FileDescriptor& event, uint64_t value)
{
  REQUIRE(::write(*event, &value, sizeof(value)) == sizeof(value));
}

TEST_CASE("execute", "[ThreadPool]")
{
  auto resource = jewels::memory::MemoryResource(std::pmr::new_delete_resource());
  auto queue = OnlineCogQueue(resource);
  auto epoll = EPollManager(resource);
  auto config = ThreadPoolConfig{
    .resource = resource,
    .thread_configs = {
      {.work = jewels::memory::make_non_null_from_ref(queue)},
      {.work = jewels::memory::make_non_null_from_ref(epoll)},
    }};
  auto pool = ThreadPool(config);

  auto executed_msgs = boost::lockfree::queue<std::optional<TestMsg>>(0);
  auto execute_cb = [&executed_msgs](const CogExecuteParams& /*params*/, const std::optional<TestMsg>& msg)
  { executed_msgs.push(msg); };

  auto msg0 = TestMsg{.value = 0};
  auto msg1 = TestMsg{.value = 1};
  auto msg2 = TestMsg{.value = 2};
  auto msg3 = TestMsg{.value = 3};

  auto cog = TestCog(jewels::memory::make_non_null_from_ref(queue));
  cog.set_execute_callback(execute_cb);
  cog.push(msg0);
  cog.push(msg1);
  cog.push(msg2);
  cog.push(msg3);

  auto env = CogEnvelope{
    .ready_time = jewels::time::SyncTime(std::chrono::seconds{1}),
    .cog = jewels::memory::make_non_null_from_ref(cog),
  };

  jewels::filesystem::FileDescriptor epoll_event{::eventfd(0, EFD_NONBLOCK)};
  bool epoll_ran = false;
  REQUIRE(epoll.add(
    *epoll_event,
    EPOLLIN | EPOLLET,
    AbstractEPollCallback::make(
      resource,
      [&epoll_event, &epoll_ran, &epoll](AbstractEPollManager& epollcb, int efd, uint32_t events)
      {
        CHECK(efd == *epoll_event);
        CHECK(events == EPOLLIN);
        CHECK(&epollcb == &epoll);
        epoll_ran = true;
      })));

  //
  // Start the pool.
  //

  pool.start();

  //
  // Execute the cog.
  //

  for (const auto& msg : {msg0, msg1, msg2, msg3})
  {
    env.ready_time += std::chrono::seconds(1);
    queue.push(env);

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

  signal_eventfd(epoll_event, 1);
  while (!epoll_ran)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  pool.stop();
  pool.join();
  CHECK(epoll_ran);
}

} // namespace
} // namespace clockwork
