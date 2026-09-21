// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/cli/exit_condition.hh"

namespace clockwork::scaffolding
{

///
/// Main entry point for clockwork executables
/// This is namespaced to allow for simple integration testing and in general ::main should just directly forward to
/// this function.
/// @param argc argument count
/// @param argv argument string array
/// @param exit optional signal for requesting an exit
///
int main(int argc, const char** argv, jewels::cli::ExitCondition& exit);

} // namespace clockwork::scaffolding
