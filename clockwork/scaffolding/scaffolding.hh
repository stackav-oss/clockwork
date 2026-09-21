// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "clockwork/common/exec_tools.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/pinion/abstract_channel_factory.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/scaffolding/abstract_casing.hh"
#include "jewels/cli/exit_condition.hh"

namespace clockwork::scaffolding
{
///
/// This applies the provided process description to the provided casing to create the executable state for this
/// process.
/// @param desc the process description
/// @param casing the casing that provides implementations
/// @param channel_factory factory used to create channels, potentially at a non-standard location
/// @param exit optional signal for requesting an exit, without this, the function will run indefinitely
/// @return EXIT_SUCCESS or EXIT_FAILURE if there was an error
///
int run(
  const Tappy<common::ProcessDescription<>>& desc,
  AbstractCasing& casing,
  pinion::AbstractChannelFactory& channel_factory,
  jewels::cli::ExitCondition& exit,
  const ExecutionParams& execution_params = ExecutionParams{});

///
/// This applies the provided process description to the provided casing to create the executable state for this
/// process while using the deterministic runner for execution. Note this should be only used for a single process
/// system.
/// @param desc the process description
/// @param casing the casing that provides implementations
/// @param channel_factory factory used to create channels, potentially at a non-standard location
/// @param exit optional signal for requesting an exit, without this, the function will run indefinitely
/// @return EXIT_SUCCESS or EXIT_FAILURE if there was an error
///
int run_deterministic(
  const Tappy<common::ProcessDescription<>>& desc,
  AbstractCasing& casing,
  pinion::AbstractChannelFactory& channel_factory,
  jewels::cli::ExitCondition& exit,
  const ExecutionParams& execution_params);
} // namespace clockwork::scaffolding
