// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/exec_tools.hh"
#include "clockwork/common/simplelaunch_runner_config_clk_cc.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/filesystem/filesystem.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/simplelaunch/v1/config.pb.h"
#include "jewels/std/expected.hh"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace jewels::simplelaunch
{

/// A helper that redirects stdout and stderr to a log file until it goes out of scope.
class RedirectOutputHelper
{
public:
  /// Attempt to construct a redirector.
  /// @param logging_directory The directory to log to.
  /// @param filesystem Filesystem handle for creating the log file.
  /// @param append Whether or not to append to the log file if it already exists.
  /// @return The redirector or an error.
  static jewels::expected<RedirectOutputHelper, jewels::filesystem::ErrorCode> make(
    const jewels::filesystem::Path& logging_directory, jewels::filesystem::Filesystem& filesystem, bool append = false);

  [[nodiscard]] const jewels::filesystem::Path& log_path() const;

  ~RedirectOutputHelper();

  RedirectOutputHelper(const RedirectOutputHelper&) = delete;
  RedirectOutputHelper(RedirectOutputHelper&&) = default;
  RedirectOutputHelper& operator=(const RedirectOutputHelper&) = delete;
  RedirectOutputHelper& operator=(RedirectOutputHelper&&) = delete;

private:
  RedirectOutputHelper(
    jewels::filesystem::Path log_path,
    jewels::filesystem::FileDescriptor stdout_fd,
    jewels::filesystem::FileDescriptor stderr_fd);

  // Path to the log file.
  jewels::filesystem::Path log_path_;

  // The file descriptors to restore from the destructor.
  jewels::filesystem::FileDescriptor saved_stdout_fd_;
  jewels::filesystem::FileDescriptor saved_stderr_fd_;
};

/// Preform a pre-flight check that none of the apps in the provided config are already running.
/// @param config the configuraiton to check.
/// @param memory_resource A memory resource for allocations.
/// @param filesystem A filesystem to use for loading configuration.
/// @return true if no apps are running.
bool check_for_running_apps(
  const ::jewels::simplelaunch::v1::Config& config,
  jewels::memory::MemoryResource memory_resource,
  jewels::filesystem::Filesystem& filesystem);

/// Prepare the commandline for a config.
/// @param config the configuraiton.
/// @param memory_resource A memory resource for allocations.
/// @param filesystem A filesystem to use for running tasks.
/// @param tmp_dir_path The directory to copy executables to when running as root.
/// @param[out] command_args Storage for the command line.
/// @return failure if sudo was requested and either a) sudo wasn't found or b) copying to @a tmp_dir_path failed.
/// @pre command_args is empty
/// @post command_args has at least one item
BinaryOutcome make_argv(
  const ::jewels::simplelaunch::v1::AppConfig& config,
  jewels::memory::MemoryResource memory_resource,
  jewels::filesystem::Filesystem& filesystem,
  const jewels::filesystem::Path& tmp_dir_path,
  std::vector<std::string>& command_args);

/// Run any pre-launch tasks in the provided config.
/// @param config the configuraiton to check.
/// @param memory_resource A memory resource for allocations.
/// @param filesystem A filesystem to use for running tasks.
/// @param[out] task_results A map to write per-task results to.
void run_pre_launch_tasks(
  const ::jewels::simplelaunch::v1::Config& config,
  jewels::memory::MemoryResource memory_resource,
  jewels::filesystem::Filesystem& filesystem,
  std::pmr::map<std::pmr::string, bool>& task_results);

/// Load configuration and start child processes.
/// @param memory_resource A memory resource for allocations.
/// @param config The configuration to launch with.
/// @param runner_config_ptr The clockwork simplelaunch runner configuration to launch with.
/// @param pre_launch_results The results of the pre-launch tasks.
/// @param pinion_args Pinion arguments for contruction the clockwork channel factory
/// @param logging_directory Path to the directory to write logs to.
/// @param listen_host The hostname to use for the command server
/// @param listen_port The TCP port to use for the command server
/// @return exit code.
int launch(
  jewels::memory::MemoryResource memory_resource,
  const ::jewels::simplelaunch::v1::Config& config,
  std::shared_ptr<clockwork::Tappy<SimplelaunchRunnerConfig>> runner_config_ptr,
  const clockwork::PinionArgs& pinion_args,
  const std::pmr::map<std::pmr::string, bool>& pre_launch_results,
  const jewels::filesystem::Path& logging_directory,
  const std::string& listen_host,
  uint16_t listen_port);

} // namespace jewels::simplelaunch
