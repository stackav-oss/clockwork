// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/scaffolding/offline_scaffolding.hh"

#include "clockwork/common/exec_tools.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/logging/log_playback/log_message_fetcher.hh"
#include "clockwork/pinion/abstract_channel_factory.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/scaffolding/abstract_casing.hh"
#include "clockwork/scaffolding/deterministic_runner_setup.hh"
#include "clockwork/scaffolding/scaffolding_offline_impl.hh"
#include "jewels/cli/exit_condition.hh"
#include "jewels/container/compare.hh"
#include "jewels/memory/pointers.hh"

namespace clockwork::scaffolding
{

int run_deterministic(
  const Tappy<common::ProcessDescription<>>& desc,
  AbstractCasing& casing,
  pinion::AbstractChannelFactory& channel_factory,
  jewels::cli::ExitCondition& exit,
  const ExecutionParams& execution_params)
{
  return run_deterministic_impl<clockwork_logging::LogMessageFetcher>(
    desc, casing, channel_factory, exit, execution_params);
}

} // namespace clockwork::scaffolding
