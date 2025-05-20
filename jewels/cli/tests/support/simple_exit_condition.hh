// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/cli/exit_condition.hh"

#include <atomic>
#include <future>

namespace jewels::cli
{

///
/// A simple implementation of ExitCondition that uses normal synchronization flags (versus the Signal version) that
/// is lighter weight and more usable in unit tests.
///
class SimpleExitCondition : public ExitCondition
{
public:
  void signal() override;
  void wait() override;
  bool check() override;

private:
  std::promise<void> promise_;
  std::atomic<bool> exit_flag_{false};
};

} // namespace jewels::cli
