// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/cli/exit_condition_signal.hh"

#include "jewels/filesystem/error_code.hh"
#include "jewels/log_cerr/log_cerr.hh"

#include <semaphore.h>

#include <atomic>
#include <cerrno>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <system_error>

namespace
{

// This is needed as a global in order to communicate between the signal handler and the application.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::atomic<jewels::cli::SignalExitCondition*> active_condition{nullptr};

extern "C" void jewels_cli_signal_exit_condition_handler(int /*unused*/)
{
  jewels::cli::SignalExitCondition* active = active_condition.load();
  if (active != nullptr)
  {
    active->handler();
  }
}

} // namespace

namespace jewels::cli
{

SignalExitCondition::SignalExitCondition()
{
  if (const int result = sem_init(&sem_, 0, 0); result == -1)
  {
    jewels::log_cerr_error("SignalExitCondition failed to create semaphore: {}", filesystem::ErrorCode{errno});
    throw std::runtime_error("SignalExitCondition failed to create semaphore");
  }
  previous_ = active_condition.exchange(this);
  SigAction action;
  ::memset(&action, 0, sizeof(SigAction));
  action.sa_handler = jewels_cli_signal_exit_condition_handler;
  ::sigaction(SIGINT, &action, &old_sigint_handler_);
  ::sigaction(SIGTERM, &action, &old_sigterm_handler_);
}

SignalExitCondition::~SignalExitCondition()
{
  if (const int result = sem_destroy(&sem_); result == -1)
  {
    jewels::log_cerr_error("SignalExitCondition failed to destroy semaphore: {}", filesystem::ErrorCode{errno});
  }
  ::sigaction(SIGINT, &old_sigint_handler_, nullptr);
  ::sigaction(SIGTERM, &old_sigterm_handler_, nullptr);
  active_condition = previous_;
}

void SignalExitCondition::signal()
{
  if (const int result = sem_post(&sem_); result == -1)
  {
    jewels::log_cerr_error("SignalExitCondition failed to signal semaphore: {}", filesystem::ErrorCode{errno});
  }
}

void SignalExitCondition::wait()
{
  while (true)
  {
    const int result = sem_wait(&sem_);
    if (result == 0)
    {
      // Re-signal since the wait will have re-locked the semaphore
      // Using an atomic bool in parallel isn't really viable since sem_post only wakes one, and sem_post isn't much
      // more overhead than an atomic bool when nothing is waiting anyways
      signal();
      return;
    }
    if (static_cast<std::errc>(errno) != std::errc::interrupted)
    {
      jewels::log_cerr_error("SignalExitCondition wait failed: {}", filesystem::ErrorCode{errno});
      return;
    }
  }
}

bool SignalExitCondition::check()
{
  while (true)
  {
    const int result = sem_trywait(&sem_);
    if (result == 0)
    {
      // Re-signal since the wait will have re-locked the semaphore
      // Using an atomic bool in parallel isn't really viable since sem_post only wakes one, and sem_post isn't much
      // more overhead than an atomic bool when nothing is waiting anyways
      signal();
      return true;
    }
    if (static_cast<std::errc>(errno) == std::errc::resource_unavailable_try_again)
    {
      return false;
    }
    if (static_cast<std::errc>(errno) != std::errc::interrupted)
    {
      jewels::log_cerr_error("SignalExitCondition trywait failed: {}", filesystem::ErrorCode{errno});
      return false;
    }
  }
}

void SignalExitCondition::handler()
{
  if (--ttl_ >= 0)
  {
    signal();
  }
  else
  {
    std::terminate();
  }
}

} // namespace jewels::cli
