// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/python/python_init.hh"
#include "clockwork/scaffolding/main_impl.hh"
#include "clockwork/scaffolding/online_scaffolding.hh" // IWYU pragma: keep
#include "jewels/cli/exit_condition_signal.hh"

int main(int argc, const char** argv)
{
  jewels::cli::SignalExitCondition exit;
  clockwork::python::python_init();
  return clockwork::scaffolding::main(argc, argv, exit);
}
