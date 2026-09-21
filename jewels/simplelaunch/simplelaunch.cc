// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/exec_tools.hh"
#include "clockwork/common/simplelaunch_runner_config_clk_cc.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/container/compare.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/filesystem.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/simplelaunch/config.hh"
#include "jewels/simplelaunch/simplelaunch_impl.hh"
#include "jewels/std/expected.hh"

#include <tclap/ArgException.h>
#include <tclap/CmdLine.h>
#include <tclap/SwitchArg.h>
#include <tclap/UnlabeledValueArg.h>
#include <tclap/ValueArg.h>

#include <cstdint>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <memory_resource>
#include <string>
#include <string_view>
#include <utility>

namespace jewels::simplelaunch
{

// NOLINTNEXTLINE(modernize-avoid-c-arrays) These are passed through directly from main().
int do_main(int argc, char* argv[])
{
  const memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  filesystem::Filesystem filesystem{memory_resource};

  TCLAP::CmdLine cmd("A simple process supervisor.", ' ', "0.1", true);
  const TCLAP::SwitchArg verbose_arg("v", "verbose", "Print additional information", cmd);
  const TCLAP::UnlabeledValueArg<std::string> config_arg(
    "config", "Path to configuration file to load", true, "", "path", cmd);
  const TCLAP::ValueArg<std::string> logging_arg(
    "l",
    "logdir",
    "Path to a directory to use for logs. Defaults to /tmp/simplelaunch_logs",
    false,
    "/tmp/simplelaunch_logs",
    "string",
    cmd);
  const TCLAP::ValueArg<std::string> host_arg(
    "", "listen", "Hostname to listen on. Defaults to 127.0.0.1", false, "127.0.0.1", "string", cmd);
  const TCLAP::ValueArg<uint16_t> port_arg(
    "p", "port", "Port to listen on. Defaults to 8080.", false, static_cast<uint16_t>(8080U), "PORT", cmd);
  const clockwork::PinionArgs pinion_args{memory_resource, cmd};

  try
  {
    cmd.parse(argc, argv);
  }
  catch (const TCLAP::ArgException& exc)
  {
    log_cerr_fatal("Failed to parse command line: {}", exc.what());
    return -1;
  }

  auto maybe_config = load_config(filesystem, config_arg.getValue());
  if (!maybe_config)
  {
    jewels::log_cerr_fatal("Failed to load config from {}: {}", config_arg.getValue(), maybe_config.error().message());
    return -1;
  }
  std::shared_ptr<clockwork::Tappy<SimplelaunchRunnerConfig>> runner_config_ptr;
  if (!maybe_config->runner_config_path().empty())
  {
    auto maybe_runner_config = clockwork::read_tachyon_config_to_heap<clockwork::Tappy<SimplelaunchRunnerConfig>>(
      maybe_config->runner_config_path());
    if (!maybe_runner_config)
    {
      jewels::log_cerr_fatal("Failed to load runner config from {}", maybe_config->runner_config_path());
      return -1;
    }
    runner_config_ptr = std::move(maybe_runner_config).value();
  }

  if (!check_for_running_apps(*maybe_config, memory_resource, filesystem))
  {
    return -1;
  }

  std::pmr::map<std::pmr::string, bool> pre_launch_task_results;
  run_pre_launch_tasks(*maybe_config, memory_resource, filesystem, pre_launch_task_results);

  const auto logging_directory_path = jewels::filesystem::Path{logging_arg.getValue(), memory_resource};
  if (verbose_arg.getValue())
  {
    jewels::log_cerr_info("Logging directory: {}", logging_directory_path.string_view());
  }

  // Redirect all stdout/stderr to the logging directory
  const auto redirector = RedirectOutputHelper::make(logging_directory_path, filesystem);
  if (!redirector)
  {
    jewels::log_cerr_fatal("Could not redirect console output, exiting");
    return -1;
  }

  return launch(
    /*memory_resource=*/memory_resource,
    /*config=*/*maybe_config,
    /*runner_config_ptr=*/std::move(runner_config_ptr),
    /*pinion_args=*/pinion_args,
    /*pre_launch_results=*/pre_launch_task_results,
    /*logging_directory=*/logging_directory_path,
    /*listen_host=*/host_arg.getValue(),
    /*listen_port=*/port_arg.getValue());
}

} // namespace jewels::simplelaunch

int main(int argc, char* argv[])
{
  try
  {
    return jewels::simplelaunch::do_main(argc, argv);
  }
  catch (std::exception& exception)
  {
    jewels::log_cerr_fatal("Unhandled exception: {}", exception.what());
    return -1;
  }
}
