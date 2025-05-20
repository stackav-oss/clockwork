// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

namespace jewels::cli
{

///
/// Interface for signaling that a loop should exit
/// Mostly intended to provide a semaphore-backed condition variable so that it's safe to use in a signal handler,
/// i.e. SignalExitCondition
///
class ExitCondition
{
public:
  ExitCondition() = default;
  ExitCondition(const ExitCondition&) = default;
  ExitCondition(ExitCondition&&) = default;
  ExitCondition& operator=(const ExitCondition&) = default;
  ExitCondition& operator=(ExitCondition&&) = default;
  virtual ~ExitCondition() = default;
  ///
  /// Signals the condition, indicating that the listener(s) should exit
  ///
  virtual void signal() = 0;
  ///
  /// Waits (forever) for the condition to be signaled
  ///
  virtual void wait() = 0;
  ///
  /// Returns true if the condition is signaled and the process should exit
  ///
  virtual bool check() = 0;
};

} // namespace jewels::cli
