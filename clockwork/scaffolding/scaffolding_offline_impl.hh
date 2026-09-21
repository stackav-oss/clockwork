// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/exec_tools.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/pinion/shm_channel_factory.hh"
#include "clockwork/scaffolding/abstract_casing.hh"
#include "jewels/cli/exit_condition.hh"

namespace clockwork::scaffolding
{

template <typename LogMessageFetcherType>
int run_deterministic_impl(
  const Tappy<common::ProcessDescription<>>& desc,
  AbstractCasing& casing,
  pinion::AbstractChannelFactory& channel_factory,
  jewels::cli::ExitCondition& exit,
  const ExecutionParams& execution_params);

} // namespace clockwork::scaffolding

#include "clockwork/scaffolding/scaffolding_offline_impl.inl"
