// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/abstract_epoll_manager.hh"
#include "clockwork/runners/epoll_manager.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_shared_ptr.hh"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <memory_resource>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <utility>

namespace clockwork
{
namespace
{

void signal_eventfd(jewels::filesystem::FileDescriptor& event, uint64_t value)
{
  REQUIRE(::write(*event, &value, sizeof(value)) == sizeof(value));
}

uint64_t unsignal_eventfd(jewels::filesystem::FileDescriptor& event)
{
  uint64_t value = 0;
  REQUIRE(::read(*event, &value, sizeof(value)) == sizeof(value));
  return value;
}

TEST_CASE("AbstractEPollManager")
{
  const jewels::memory::MemoryResource memres{std::pmr::get_default_resource()};
  EPollManager manager{memres};

  bool ran_a = false;
  bool ran_b = false;
  bool ran_c = false;

  jewels::filesystem::FileDescriptor event_a{::eventfd(0, EFD_NONBLOCK)};
  jewels::filesystem::FileDescriptor event_b{::eventfd(0, EFD_NONBLOCK)};
  jewels::filesystem::FileDescriptor event_c{::eventfd(0, EFD_NONBLOCK)};
  REQUIRE(event_a);
  REQUIRE(event_b);
  REQUIRE(event_c);

  REQUIRE(manager.add(
    *event_a,
    EPOLLIN,
    AbstractEPollCallback::make(
      memres,
      [&event_a, &ran_a](AbstractEPollManager& epoll, int efd, uint32_t events)
      {
        CHECK(unsignal_eventfd(event_a));
        CHECK(efd == *event_a);
        CHECK(events == EPOLLIN);
        ran_a = true;
        epoll.remove(efd);
      })));
  REQUIRE(manager.add(
    *event_b,
    EPOLLIN,
    AbstractEPollCallback::make(
      memres,
      [&event_b, &ran_b](AbstractEPollManager& /*epoll*/, int efd, uint32_t events)
      {
        CHECK(unsignal_eventfd(event_b));
        CHECK(efd == *event_b);
        CHECK(events == EPOLLIN);
        ran_b = true;
      })));
  REQUIRE(manager.add(
    *event_c,
    EPOLLIN,
    AbstractEPollCallback::make(
      memres,
      [&event_c, &ran_c](AbstractEPollManager& epoll, int efd, uint32_t events)
      {
        CHECK(unsignal_eventfd(event_c));
        CHECK(efd == *event_c);
        CHECK(events == EPOLLIN);
        ran_c = true;
        CHECK(epoll.modify(efd, 0));
      })));

  // signal b only
  signal_eventfd(event_b, 1);
  CHECK(manager.wait(std::chrono::milliseconds(50)));
  CHECK(!std::exchange(ran_a, false));
  CHECK(std::exchange(ran_b, false));
  CHECK(!std::exchange(ran_c, false));

  // signal all three, expect callbacks to run
  signal_eventfd(event_a, 1);
  signal_eventfd(event_b, 1);
  signal_eventfd(event_c, 1);
  CHECK(manager.wait(std::chrono::milliseconds(-1)));
  CHECK(std::exchange(ran_a, false));
  CHECK(std::exchange(ran_b, false));
  CHECK(std::exchange(ran_c, false));

  // callback `a` removes itself so shouldn't trigger anymore
  // callback 'c' modifies itself so shouldn't trigger anymore
  signal_eventfd(event_a, 1);
  signal_eventfd(event_b, 1);
  signal_eventfd(event_c, 1);
  CHECK(manager.wait(std::chrono::milliseconds(50)));
  CHECK(!std::exchange(ran_a, false));
  CHECK(std::exchange(ran_b, false));
  CHECK(!std::exchange(ran_c, false));

  // no signals, no runs
  CHECK(manager.wait(std::chrono::milliseconds(50)));
  CHECK(!std::exchange(ran_a, false));
  CHECK(!std::exchange(ran_b, false));
  CHECK(!std::exchange(ran_c, false));

  manager.remove(*event_a);
  manager.remove(*event_b);
  manager.remove(*event_c);
}

} // namespace
} // namespace clockwork
