// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/exec_tools.hh"
#include "clockwork/pinion/shm_channel_factory.hh"
#include "clockwork/pinion/tcp_bridge.hh"
#include "clockwork/pinion/tcp_bridge_config_clk_cc.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/container/compare.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"

#include <tclap/CmdLine.h>
#include <tclap/UnlabeledValueArg.h>

#include <cstdlib>
#include <exception>
#include <memory_resource>
#include <string>
#include <utility>

namespace clockwork::pinion::tcp_bridge
{

int main(int argc, const char** argv)
{
  const auto memres = jewels::memory::MemoryResource(std::pmr::get_default_resource());
  TCLAP::CmdLine cmd("Clockwork TCP bridge", ' ', "1.0", true);
  const TCLAP::UnlabeledValueArg<std::string> arg_config_file(
    "config", "the bridge configuration file path", true, "", "string", cmd);
  const PinionArgs pinion_args{memres, cmd};

  try
  {
    cmd.parse(argc, argv);
  }
  catch (const std::exception& exc)
  {
    jewels::log_cerr_error("Caught exception parsing command line: {}", exc.what());
    return EXIT_FAILURE;
  }

  auto config = read_tachyon_config<Tappy<TcpBridgeConfig<>>>(arg_config_file.getValue());
  if (!config)
  {
    return EXIT_FAILURE;
  }

  auto channel_factory = pinion_args.make_factory();
  if (!channel_factory)
  {
    jewels::log_cerr_error("failed to create channel factory");
    return EXIT_FAILURE;
  }

  TcpBridge bridge{memres, *std::move(channel_factory), *config};

  if (!ok(bridge.initialize()))
  {
    return EXIT_FAILURE;
  }

  if (!ok(bridge.run()))
  {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

} // namespace clockwork::pinion::tcp_bridge

int main(int argc, const char** argv)
{
  return clockwork::pinion::tcp_bridge::main(argc, argv);
}
