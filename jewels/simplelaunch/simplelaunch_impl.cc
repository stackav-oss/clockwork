// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/simplelaunch/simplelaunch_impl.hh"

#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/filesystem/filesystem.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/simplelaunch/config.hh"
#include "jewels/simplelaunch/simplelaunch_runner.hh"
#include "jewels/std/expected.hh"
#include "jewels/utility/fix_clockwork_path.hh"

#include <boost/fusion/algorithm/iteration/for_each.hpp>
#include <boost/fusion/sequence/intrinsic/at_key.hpp>
#include <boost/process/v1/system.hpp>
#include <fmt/format.h>
#include <google/protobuf/repeated_ptr_field.h>

#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <functional>
#include <map>
#include <memory_resource>
#include <string>
#include <string_view>
#include <tuple>
#include <unistd.h>
#include <unordered_set>
#include <utility>
#include <vector>

namespace jewels::simplelaunch
{

namespace
{

/// Get a set of the currently running process names.
jewels::expected<std::pmr::unordered_set<std::pmr::string>, jewels::filesystem::ErrorCode>
get_running_processes(jewels::filesystem::Filesystem& filesystem, jewels::memory::MemoryResource memory_resource)
{
  const jewels::filesystem::Path proc_dir{"/proc", memory_resource};
  const auto maybe_proc = filesystem.read_directory(proc_dir);
  if (!maybe_proc)
  {
    jewels::log_cerr_fatal("Failed to scan /proc: {}", maybe_proc.error().message());
    return jewels::unexpected{maybe_proc.error()};
  }

  std::pmr::unordered_set<std::pmr::string> running_processes{memory_resource};
  for (const auto& process_dir : maybe_proc.value())
  {
    // If the directory name isn't a digit then it's not a process information
    // directory
    if (std::isdigit(process_dir.filename_view()[0]) == 0)
    {
      continue;
    }

    const auto process_exe_path = proc_dir / process_dir / "exe";
    auto maybe_executable = filesystem.read_symlink(process_exe_path);
    if (!maybe_executable)
    {
      // Some sylinks (like PID 1) may return a permissions error.
      // There may also be races where a process exits between the directory
      // list call and the resolve call resulting in ENOENT
      if (maybe_executable.error().value() == EACCES || maybe_executable.error().value() == ENOENT)
      {
        continue;
      }

      jewels::log_cerr_fatal("Failed read {}: {}", process_exe_path.string_view(), maybe_executable.error().message());
      return jewels::unexpected{maybe_executable.error()};
    }

    const jewels::filesystem::Path executable_path{*std::move(maybe_executable), memory_resource};
    running_processes.emplace(executable_path.filename().string());
  }

  return {running_processes};
}

/// Run a pre-launch task.
int32_t run_pre_launch_task(
  const AppConfig& app_config,
  jewels::filesystem::Filesystem& filesystem,
  jewels::memory::MemoryResource memory_resource)
{
  const auto tmp_dir = jewels::filesystem::Path{"/tmp", memory_resource};
  std::vector<std::string> command_args;
  if (fails(make_argv(app_config, memory_resource, filesystem, tmp_dir, command_args)))
  {
    return -1;
  }

  const auto pre_launch_app_result = boost::process::v1::system(command_args);

  if (pre_launch_app_result != 0)
  {
    jewels::log_cerr_fatal(
      "Pre-launch application {} failed with exit code {}", app_config.name(), pre_launch_app_result);
  }
  return pre_launch_app_result;
}

} // namespace

BinaryOutcome make_argv(
  const AppConfig& config,
  jewels::memory::MemoryResource memory_resource,
  jewels::filesystem::Filesystem& filesystem,
  const jewels::filesystem::Path& tmp_dir_path,
  std::vector<std::string>& command_args)
{
  auto binary = std::pmr::string{config.executable(), memory_resource};
  if (config.as_root())
  {
    const auto maybe_sudo = filesystem.search_path("sudo");
    if (!maybe_sudo)
    {
      jewels::log_cerr_fatal("Failed to find sudo: {}", maybe_sudo.error().message());
      return failure;
    }
    binary = maybe_sudo->string_view();
  }
  const auto binary_path = fix_clockwork_path(binary);
  command_args.emplace_back(binary_path);
  if (config.as_root())
  {
    // We have to copy the executable to a path that is readable by root because by default FUSE filesystem mounts are
    // not.
    const jewels::filesystem::Path executable_path{config.executable(), memory_resource};
    const auto temp_file_path = tmp_dir_path / executable_path.filename().string_view();
    std::ignore = filesystem.remove(temp_file_path.string_view());
    if (const auto copy_result = filesystem.copy_file(config.executable(), temp_file_path.string_view()); !copy_result)
    {
      jewels::log_cerr_fatal(
        "Failed to copy {} to {}: {}",
        config.executable(),
        temp_file_path.string_view(),
        copy_result.error().message());
      return failure;
    }

    command_args.emplace_back("--preserve-env");
    command_args.emplace_back("--non-interactive");
    command_args.emplace_back(temp_file_path);
  }

  for (const auto& arg : config.args())
  {
    command_args.emplace_back(arg);
  }
  return success;
}

jewels::expected<RedirectOutputHelper, jewels::filesystem::ErrorCode> RedirectOutputHelper::make(
  const jewels::filesystem::Path& logging_directory, jewels::filesystem::Filesystem& filesystem, bool append)
{
  jewels::filesystem::FileDescriptor old_stdout_fd{dup(STDOUT_FILENO)};
  jewels::filesystem::FileDescriptor old_stderr_fd{dup(STDERR_FILENO)};

  /// The permissions to set on log files.
  constexpr int32_t log_file_permissions = 0644;

  if (auto dir_result = filesystem.is_directory(logging_directory); dir_result && !*dir_result)
  {
    if (const auto create_dir_expected = filesystem.create_directories(logging_directory); !create_dir_expected)
    {
      jewels::log_cerr_fatal(
        "Could not create {}: {}", logging_directory.string_view(), create_dir_expected.error().message());
      return jewels::unexpected{create_dir_expected.error()};
    }
  }
  else if (!dir_result)
  {
    jewels::log_cerr_fatal("Failed to stat {}: {}", logging_directory.string_view(), dir_result.error().message());
    return jewels::unexpected{dir_result.error()};
  }

  auto open_flags = O_WRONLY | O_CREAT;
  // NOLINTNEXTLINE(hicpp-signed-bitwise) open(2) only accepts signed flags.
  open_flags |= append ? O_APPEND : O_TRUNC;
  auto file_path = logging_directory / fmt::format("simplelaunch_{}.log", getpid());
  auto expected_fd = filesystem.open(file_path, open_flags, log_file_permissions);
  if (!expected_fd)
  {
    jewels::log_cerr_info("Error opening log file {}: {}", file_path.string_view(), expected_fd.error().message());
    return jewels::unexpected{jewels::filesystem::ErrorCode{errno}};
  }

  // This will be the last message displayed to stdout, so let's make it easy to
  // determine where the log is.
  jewels::log_cerr_info("Simple Launch console output file: {}", file_path.string());
  const auto file_descriptor = expected_fd.value().release();
  if (dup2(file_descriptor, STDOUT_FILENO) == -1 || dup2(file_descriptor, STDERR_FILENO) == -1)
  {
    jewels::log_cerr_error("Error from console dup2: {}", errno);
    return jewels::unexpected{jewels::filesystem::ErrorCode{errno}};
  }

  return RedirectOutputHelper{file_path, std::move(old_stdout_fd), std::move(old_stderr_fd)};
}

const jewels::filesystem::Path& RedirectOutputHelper::log_path() const
{
  return log_path_;
}

RedirectOutputHelper::~RedirectOutputHelper()
{
  if (!saved_stdout_fd_ || !saved_stderr_fd_)
  {
    return;
  }

  // nothing we can do about failures here...
  static_cast<void>(fflush(stdout));
  static_cast<void>(dup2(*saved_stdout_fd_, STDOUT_FILENO));
  static_cast<void>(fflush(stderr));
  static_cast<void>(dup2(*saved_stderr_fd_, STDERR_FILENO));
}

RedirectOutputHelper::RedirectOutputHelper(
  jewels::filesystem::Path log_path,
  jewels::filesystem::FileDescriptor stdout_fd,
  jewels::filesystem::FileDescriptor stderr_fd)
  : log_path_{std::move(log_path)}, saved_stdout_fd_{std::move(stdout_fd)}, saved_stderr_fd_{std::move(stderr_fd)}
{
}

bool check_for_running_apps(
  const ::jewels::simplelaunch::v1::Config& config,
  jewels::memory::MemoryResource memory_resource,
  jewels::filesystem::Filesystem& filesystem)
{
  const auto expected_running_processes = get_running_processes(filesystem, memory_resource);
  if (!expected_running_processes)
  {
    return false;
  }

  std::pmr::unordered_set<std::pmr::string> already_running_apps{memory_resource};
  for (const auto& app : config.app())
  {
    const auto process_executable_path =
      jewels::filesystem::Path{fix_clockwork_path(app.executable()), memory_resource};
    auto name = process_executable_path.filename().string();
    if (expected_running_processes->contains(name))
    {
      already_running_apps.emplace(std::move(name));
    }
  }

  if (!already_running_apps.empty())
  {
    jewels::log_cerr_warn("The following applications are already running:");
    for (const auto& name : already_running_apps)
    {
      jewels::log_cerr_warn("  {}", name);
    }
    return false;
  }

  return true;
}

void run_pre_launch_tasks(
  const ::jewels::simplelaunch::v1::Config& config,
  jewels::memory::MemoryResource memory_resource,
  jewels::filesystem::Filesystem& filesystem,
  std::pmr::map<std::pmr::string, bool>& task_results)
{
  ;
  for (const auto& pre_launch_app : config.pre_launch())
  {
    std::pmr::string task_name{memory_resource};
    task_name = pre_launch_app.name();
    const bool task_suceeded = run_pre_launch_task(pre_launch_app, filesystem, memory_resource) == 0;
    task_results[task_name] = task_suceeded;
  }
}

// NOLINTNEXTLINE(readability-function-size) Using keyword comments to manage complexity
int launch(
  jewels::memory::MemoryResource memory_resource,
  const ::jewels::simplelaunch::v1::Config& config,
  std::shared_ptr<clockwork::Tappy<SimplelaunchRunnerConfig>> runner_config_ptr,
  const clockwork::PinionArgs& pinion_args,
  const std::pmr::map<std::pmr::string, bool>& pre_launch_results,
  const jewels::filesystem::Path& logging_directory,
  const std::string& listen_host,
  uint16_t listen_port)
{
  const auto channel_factory_result = pinion_args.make_factory({}, {}, {});
  if (!channel_factory_result)
  {
    jewels::log_cerr_error("Failed to create the pinion channel factory");
    return -1;
  }

  SimplelaunchRunner runner{SimplelaunchRunner::CtorParams{
    .memory_resource = memory_resource,
    .config = config,
    .runner_config_ptr = std::move(runner_config_ptr),
    .channel_factory_ptr = *channel_factory_result,
    .pre_launch_results = pre_launch_results,
    .logging_directory = logging_directory,
    .listen_host = listen_host,
    .listen_port = listen_port,
  }};

  if (!ok(runner.initialize()))
  {
    return -1;
  }

  runner.run();

  return 0;
}

} // namespace jewels::simplelaunch
