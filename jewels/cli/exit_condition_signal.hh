// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "jewels/cli/exit_condition.hh"

#include <semaphore.h>

#include <atomic>
#include <csignal> // IWYU pragma: keep
#include <cstdint>

extern "C" void jewels_cli_signal_exit_condition_handler(int /*unused*/);

namespace jewels::cli
{

///
/// Signal handling implementation of ExitCondition
///
class SignalExitCondition : public ExitCondition
{
  using HandlerType = void(int);
  using SigAction = struct sigaction;

public:
  SignalExitCondition();
  SignalExitCondition(const SignalExitCondition&) = delete;
  SignalExitCondition(SignalExitCondition&&) = delete;
  SignalExitCondition& operator=(const SignalExitCondition&) = delete;
  SignalExitCondition& operator=(SignalExitCondition&&) = delete;
  ~SignalExitCondition() override;

  ///
  /// Signals the condition, indicating that the listener(s) should exit
  ///
  void signal() override;
  ///
  /// Waits (forever) for the condition to be signaled
  ///
  void wait() override;
  ///
  /// Returns true if the condition is signaled and the process should exit
  ///
  bool check() override;

private:
  friend void ::jewels_cli_signal_exit_condition_handler(int /*unused*/);
  void handler();

  SignalExitCondition* previous_;
  SigAction old_sigint_handler_{};
  SigAction old_sigterm_handler_{};
  std::atomic<int32_t> ttl_{3};
  sem_t sem_{};
};

} // namespace jewels::cli
