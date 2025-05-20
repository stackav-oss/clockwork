// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/cli/exit_condition_signal.hh"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <csetjmp>
#include <csignal>
#include <exception>
#include <thread>
#include <tuple>

namespace
{

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)  For testing purposes only
jmp_buf terminate_handler_jmp;

void terminate_handler()
{
  // Because all the std::abort / std::terminate code assert no-return up after doing the hookable thing (SIGABRT,
  // terminate_handler) the only real option for testing is to longjmp out.  Sketchy, but usable for a test.
  static std::atomic<int> terminate_count{0};
  // NOLINTNEXTLINE(cert-err52-cpp, cppcoreguidelines-pro-bounds-array-to-pointer-decay)  For testing purposes only
  longjmp(terminate_handler_jmp, ++terminate_count);
}

} // namespace

namespace jewels::cli
{
namespace
{

TEST_CASE("main")
{
  SignalExitCondition condition1;
  CHECK(!condition1.check());
  {
    SignalExitCondition condition2;
    CHECK(!condition2.check());
    CHECK(std::raise(SIGINT) == 0);
    CHECK(condition2.check());
    CHECK(condition2.check());
    CHECK(condition2.check());
    condition2.wait();
  }
  CHECK(!condition1.check());
  std::thread sigthread(
    []()
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      std::ignore = std::raise(SIGINT);
    });
  condition1.wait();
  CHECK(condition1.check());
  sigthread.join();
}

TEST_CASE("abort after multi")
{
  int path = 0;
  SignalExitCondition condition;
  auto terminate_handler_old = std::set_terminate(terminate_handler);
  CHECK(!condition.check());
  // For whatever reason it seems that SIGINT isn't caught anymore after the std::terminate, which is okay but does mean
  // this can't test continuted signal handling
  // NOLINTNEXTLINE(cert-err52-cpp, cppcoreguidelines-pro-bounds-array-to-pointer-decay)  For testing purposes only
  if (!setjmp(terminate_handler_jmp))
  {
    CHECK(std::raise(SIGINT) == 0);
    CHECK(condition.check());
    CHECK(std::raise(SIGINT) == 0);
    CHECK(std::raise(SIGINT) == 0);
    CHECK(std::raise(SIGINT) == 0);
    CHECK(false);
  }
  else
  {
    path = 1;
  }
  CHECK(path == 1);
  std::set_terminate(terminate_handler_old);
}

} // namespace
} // namespace jewels::cli
