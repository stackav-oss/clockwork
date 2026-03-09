// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/simplelaunch/simplelaunch_impl.hh"

#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/filesystem/filesystem.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/simplelaunch/config.hh"
#include "jewels/simplelaunch/service.hh"
#include "jewels/std/expected.hh"
#include "jewels/utility/fix_clockwork_path.hh"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/filesystem.hpp>
#include <boost/process/args.hpp>
#include <boost/process/search_path.hpp>
#include <boost/process/system.hpp>
#include <boost/system/errc.hpp>
#include <fmt/format.h>
#include <google/protobuf/repeated_ptr_field.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <filesystem>
#include <functional>
#include <memory_resource>
#include <string>
#include <string_view>
#include <unistd.h>
#include <unordered_map>
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
int32_t run_pre_launch_task(const AppConfig& app_config)
{
  const auto binary = app_config.as_root() ? boost::process::search_path("sudo").native() : app_config.executable();
  const auto binary_path = fix_clockwork_path(binary);
  std::vector<std::string> command_args;
  if (app_config.as_root())
  {
    // We have to copy the executable to a path that is readable by root because by default FUSE filesystem mounts are
    // not.
    const auto temp_file_path =
      std::filesystem::temp_directory_path() / std::filesystem::path(app_config.executable()).filename();
    std::filesystem::remove(temp_file_path);
    std::filesystem::copy_file(
      app_config.executable(), temp_file_path, std::filesystem::copy_options::overwrite_existing);

    command_args.emplace_back("--preserve-env");
    command_args.emplace_back("--non-interactive");
    command_args.emplace_back(temp_file_path);
  }

  for (const auto& arg : app_config.args())
  {
    command_args.emplace_back(arg);
  }

  const auto pre_launch_app_result = boost::process::system(binary_path, boost::process::args(command_args));

  if (pre_launch_app_result != 0)
  {
    jewels::log_cerr_fatal(
      "Pre-launch application {} failed with exit code {}", app_config.name(), pre_launch_app_result);
  }
  return pre_launch_app_result;
}

} // namespace

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
  std::pmr::unordered_map<std::pmr::string, bool>& task_results)
{
  ;
  for (const auto& pre_launch_app : config.pre_launch())
  {
    std::pmr::string task_name{memory_resource};
    task_name = pre_launch_app.name();
    const bool task_suceeded = run_pre_launch_task(pre_launch_app) == 0;
    task_results[task_name] = task_suceeded;
  }
}

int launch(
  jewels::memory::MemoryResource memory_resource,
  const ::jewels::simplelaunch::v1::Config& config,
  const std::pmr::unordered_map<std::pmr::string, bool>& pre_launch_results,
  const jewels::filesystem::Path& logging_directory,
  const std::string& listen_host,
  uint16_t listen_port)
{
  // The io_context is required for all I/O
  boost::asio::io_context io_context{1};
  const boost::asio::ip::tcp::endpoint endpoint{boost::asio::ip::make_address(listen_host), listen_port};

  TaskManagerImpl task_manager{
    config, logging_directory, memory_resource, jewels::memory::make_non_null_from_ref(io_context), pre_launch_results};

  // Create the HTTP server
  if (const auto http_server_expected = task_manager.create_http_server(endpoint); !http_server_expected)
  {
    jewels::log_cerr_fatal("Failed to create http server: {}", http_server_expected.error().message());
    return -1;
  }

  jewels::log_cerr_info("Server listening on http://{}:{}", listen_host, listen_port);
  task_manager.register_signal_handlers();
  auto start_up_succeeded =
    std::all_of(pre_launch_results.begin(), pre_launch_results.end(), [](auto& entry) -> bool { return entry.second; });
  if (start_up_succeeded)
  {
    task_manager.spawn_subprocesses();
  }

  io_context.run();

  return 0;
}

} // namespace jewels::simplelaunch
