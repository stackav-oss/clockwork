// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/cli/exit_condition_signal.hh"

#include <cstdlib>

int main()
{
  jewels::cli::SignalExitCondition exit;
  exit.wait();
  return EXIT_SUCCESS;
}
