// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/simplelaunch/child_process.hh"

#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/filesystem/filesystem.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/simplelaunch/config.hh"
#include "jewels/simplelaunch/cpu_affinity.hh"
#include "jewels/simplelaunch/service_definition.hh"
#include "jewels/std/expected.hh"
#include "jewels/utility/fix_clockwork_path.hh"

#include <boost/asio/basic_deadline_timer.hpp>
#include <boost/date_time/posix_time/posix_time_duration.hpp>
#include <boost/system/errc.hpp>
#include <fmt10/format.h>

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <iterator>
#include <map>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <unistd.h>
#include <utility>
#include <vector>

namespace jewels::simplelaunch
{
namespace
{
/// The permissions to set on log files.
constexpr int32_t log_file_permissions = 0644;

std::pmr::vector<std::pmr::string>
build_c_envp_from(const AppConfig& current_app, jewels::memory::MemoryResource memory_resource)
{
  // For now, next we'll want to bring these in and merge their values with the config
  std::map<std::string_view, std::string_view> combined_env;
  for (char** env = environ; *env != nullptr; env = std::next(env))
  {
    // Split the "key=value" string into "key" and "value" to populate the combined_env map
    const std::string_view current_env{*env};
    const auto first_equals = current_env.find_first_of('=');
    const auto key = current_env.substr(0, first_equals);
    const auto value = current_env.substr(first_equals + 1);
    combined_env[key] = value;
  }
  combined_env.insert(current_app.env().begin(), current_app.env().end());

  std::pmr::vector<std::pmr::string> envs{memory_resource};
  envs.reserve(combined_env.size());
  for (auto& [key, value] : combined_env)
  {
    envs.emplace_back(fmt::format("{}={}", key, value));
  }

  return envs;
}
} // namespace

ChildProcessInfo::ChildProcessInfo(
  AppConfig config,
  jewels::filesystem::Path logging_directory,
  jewels::memory::MemoryResource memory_resource,
  jewels::memory::ObjectPtr<boost::asio::io_context> io_ctx_ptr) noexcept
  : config_{std::move(config)},
    logging_directory_(std::move(logging_directory)),
    log_file_{memory_resource},
    memory_resource_{std::move(memory_resource)},
    io_ctx_ptr_{io_ctx_ptr}
{
  process_info_.set_name(config_.name());
  set_state(ProcessState::not_running, false, 0);
}

const ProcessInfo& ChildProcessInfo::get_process_info() const noexcept
{
  return process_info_;
}

void ChildProcessInfo::send_signal(int signal_value) const noexcept
{
  kill(process_info_.pid(), signal_value);
}

void ChildProcessInfo::stop_process()
{
  const auto pid = process_info_.pid();
  maybe_timer_.emplace(boost::asio::deadline_timer(*io_ctx_ptr_, boost::posix_time::seconds(3)));
  maybe_timer_->async_wait(
    [pid](boost::system::error_code error_code)
    {
      // An error code being present means the timer was canceled due to the child process exiting.
      if (error_code)
      {
        return;
      }
      jewels::log_cerr_warn("PID {} did not exit for SIGINT - sending SIGTERM now", pid);
      kill(pid, SIGTERM);
    });

  kill(pid, SIGINT);
}

void ChildProcessInfo::set_state(ProcessState state, bool core_dumped, int32_t /*why*/) noexcept
{
  process_info_.set_state(static_cast<::jewels::simplelaunch::v1::ProcessState>(state));
  process_info_.set_core_dumped(core_dumped);
  if (core_dumped)
  {
    jewels::filesystem::Filesystem fs_interface{memory_resource_};
    const jewels::filesystem::Path file_path{
      fmt::format("/run/user/{}/simple_launch/{}/crashed", getuid(), getpid()), memory_resource_};
    jewels::log_cerr_warn("Creating crash notification file at {}", file_path.string_view());
    auto expected_touch = fs_interface.touch(file_path);
    if (!expected_touch)
    {
      jewels::log_cerr_error("Failed to create crash notification file at {}", file_path.string_view());
    }
  }
}

[[nodiscard]] const jewels::filesystem::Path& ChildProcessInfo::get_log_file() const noexcept
{
  return log_file_;
}

void ChildProcessInfo::handle_process_exit()
{
  if (maybe_timer_)
  {
    maybe_timer_->cancel();
  }
}

jewels::filesystem::Path ChildProcessInfo::create_output_file_name() const noexcept
{
  jewels::filesystem::Filesystem fs_interface{memory_resource_};
  jewels::filesystem::Path file_path{memory_resource_};

  // Find a file name to write to based on how many restarts this task has had.
  for (size_t run_number = 0;; run_number += 1)
  {
    file_path =
      logging_directory_ /
      fmt::format(
        "{}_{}.log", jewels::filesystem::Path(config_.name(), memory_resource_).filename().string(), run_number);

    // If an error occurs in the exists call then just return that file path and let the downstream open call deal with
    // it.
    if (!fs_interface.exists(file_path).value_or(false))
    {
      break;
    }
  };

  return file_path;
}

bool ChildProcessInfo::has_exited() const noexcept
{
  const auto state = static_cast<ProcessState>(process_info_.state());
  return state == ProcessState::exited || state == ProcessState::crashed;
}

void ChildProcessInfo::start() noexcept
{
  jewels::log_cerr_debug("Starting {}", config_.executable());

  // Get the log file path before forking.
  auto log_output = create_output_file_name();
  log_file_ = log_output;

  // We'll fork, the parent will keep the returned pid, the child will prepare for exec based on the arguments in the
  // AppConfig
  const auto pid = ::fork();
  if (pid != 0)
  {
    // This means we're the launcher, send to stdout what we just did
    jewels::log_cerr_debug("Started {} has PID {}", config_.executable(), pid);
    process_info_.set_pid(pid);
    set_state(ProcessState::running, false, 0);
    return;
  }

  // This branch is inside the forked child process

  // Pull our child out of the same session so it doesn't share it with the parent
  setsid();

  // Set up log output file
  jewels::filesystem::Filesystem filesystem{memory_resource_};
  auto expected_fd = filesystem.open(log_output, O_WRONLY | O_TRUNC | O_CREAT, log_file_permissions);
  if (!expected_fd)
  {
    jewels::log_cerr_error("Error opening output file: {}", expected_fd.error().message());
  }

  // Dup the file descriptor to stdout and stderr
  const auto file_descriptor = expected_fd.value().release();
  if (dup2(file_descriptor, STDOUT_FILENO) == -1)
  {
    jewels::log_cerr_error("Error from STDOUT dup2: {}", errno);
  }
  if (dup2(file_descriptor, STDERR_FILENO) == -1)
  {
    jewels::log_cerr_error("Error from STDERR dup2: {}", errno);
  }

  // Populate envp
  auto envp_data = build_c_envp_from(config_, memory_resource_);
  std::pmr::vector<char*> envp{memory_resource_};
  // Reserve enough space for the environment variables and a nullptr at the end
  envp.reserve(envp_data.size() + 1UL);

  // Populate envp with pointers
  for (auto& env : envp_data)
  {
    envp.push_back(env.data());
  }
  envp.push_back(nullptr);

  // Populate argv
  std::pmr::vector<char*> argv{memory_resource_};
  // Reserve enough space for adding the executable name as the first argument and a nullptr at the end
  argv.reserve(static_cast<size_t>(config_.args_size()) + 2UL);
  argv.emplace_back(config_.mutable_executable()->data());
  for (int32_t index = 0; index < config_.args_size(); index++)
  {
    argv.emplace_back(config_.mutable_args(index)->data());
  }
  argv.push_back(nullptr);
  const auto executable_path = fix_clockwork_path(config_.executable());

  if (!set_cpu_affinity(std::span{config_.cpus().data(), static_cast<size_t>(config_.cpus_size())}))
  {
    jewels::log_cerr_fatal("Failed setting CPU affinities for {}", config_.executable());
    // This method is only called in a single-threaded context.
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    std::exit(1);
  }

  if (::execve(executable_path.c_str(), argv.data(), envp.data()) == -1)
  {
    jewels::log_cerr_fatal(
      "Error launching {}: {}", config_.executable(), jewels::filesystem::ErrorCode{errno}.message());
    // This method is only called in a single-threaded context.
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    std::exit(1);
  }
}

} // namespace jewels::simplelaunch
