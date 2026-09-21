// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/cli/tests/support/simple_exit_condition.hh"

namespace jewels::cli
{

void SimpleExitCondition::signal()
{
  if (!exit_flag_.exchange(true))
  {
    promise_.set_value();
  }
}

void SimpleExitCondition::wait()
{
  promise_.get_future().wait();
}

bool SimpleExitCondition::check()
{
  return exit_flag_;
}

} // namespace jewels::cli
