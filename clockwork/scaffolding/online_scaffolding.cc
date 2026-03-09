// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/scaffolding/online_scaffolding.hh"

#include "clockwork/common/exec_tools.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/pinion/shm_channel_factory.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/scaffolding/abstract_casing.hh"
#include "jewels/cli/exit_condition.hh"
#include "jewels/container/compare.hh"
#include "jewels/log_cerr/log_cerr.hh"

#include <cstdlib>

namespace clockwork::scaffolding
{

int run_deterministic(
  const Tappy<common::ProcessDescription<>>& /*desc*/,
  AbstractCasing& /*casing*/,
  pinion::ShmChannelFactory& /*channel_factory*/,
  jewels::cli::ExitCondition& /*exit*/,
  const ExecutionParams& /*execution_params*/)
{
  jewels::log_cerr_error("Deterministic executions not supported by this binary");
  return EXIT_FAILURE;
}

} // namespace clockwork::scaffolding
