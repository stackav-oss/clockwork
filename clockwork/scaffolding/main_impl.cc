// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/scaffolding/main_impl.hh"

#include "clockwork/common/exec_tools.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/scaffolding/abstract_casing.hh"
#include "clockwork/scaffolding/end_process_exception.hh"
#include "clockwork/scaffolding/scaffolding.hh"
#include "jewels/container/compare.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"

#include <tclap/ArgException.h>
#include <tclap/CmdLine.h>
#include <tclap/UnlabeledValueArg.h>

#include <cstdlib>
#include <memory>
#include <memory_resource>
#include <string>

namespace clockwork::scaffolding
{

int main(int argc, const char** argv, jewels::cli::ExitCondition& exit)
{
  auto memres_channels = jewels::memory::MemoryResource(std::pmr::get_default_resource());
  auto memres_casing = jewels::memory::MemoryResource(std::pmr::get_default_resource());
  TCLAP::CmdLine cmd("Run a clockwork process", ' ', "1.0", true);
  const TCLAP::UnlabeledValueArg<std::string> arg_desc_file(
    "config", "the process description file path", true, "", "string", cmd);
  const PinionArgs pinion_args{memres_channels, cmd};
  const ExecutionArgs execution_args{cmd};
  try
  {
    cmd.parse(argc, argv);
  }
  catch (const TCLAP::ArgException& exc)
  {
    jewels::log_cerr_error("Failed to parse command line: {}", exc.what());
    return EXIT_FAILURE;
  }

  auto desc = read_tachyon_config_to_heap<Tappy<common::ProcessDescription<>>>(arg_desc_file.getValue());
  if (!desc)
  {
    return EXIT_FAILURE;
  }

  auto casing = make_casing(memres_casing);
  if (!casing)
  {
    jewels::log_cerr_error("failed to make casing");
    return EXIT_FAILURE;
  }

  auto channel_factory = pinion_args.make_factory();
  if (!channel_factory)
  {
    jewels::log_cerr_error("failed to create channel factory");
    return EXIT_FAILURE;
  }

  auto execution_params = execution_args.make_execution_params();
  if (!execution_params)
  {
    jewels::log_cerr_error("failed to create sim params");
    return EXIT_FAILURE;
  }
  try
  {
    if (execution_params->execution_mode == ExecutionMode::deterministic)
    {
      return run_deterministic(**desc, *casing, *channel_factory, exit, *execution_params);
    }
    return run(**desc, *casing, *channel_factory, exit, *execution_params);
  }
  catch (const EndProcessException& e)
  {
    return e.return_code();
  }
}

} // namespace clockwork::scaffolding
