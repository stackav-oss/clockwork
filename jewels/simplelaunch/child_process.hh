// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/simplelaunch/config.hh"
#include "jewels/simplelaunch/service_definition.hh"

#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/io_context.hpp>

#include <cstdint>
#include <optional>

namespace jewels::simplelaunch
{
/// Information about a child process.
class ChildProcessInfo
{
public:
  /// Constructor.
  /// @param[in] config The configuration to use.
  /// @param[in] logging_directory The directory to place process log output files in.
  /// @param[in] memory_resource The memory resource to use.
  /// @param[in] io_ctx_ptr A pointer to the Boost asio context.
  ChildProcessInfo(
    AppConfig config,
    filesystem::Path logging_directory,
    memory::MemoryResource memory_resource,
    memory::ObjectPtr<boost::asio::io_context> io_ctx_ptr) noexcept;

  /// Default destructor.
  ~ChildProcessInfo() = default;

  // Don't allow default construction.
  ChildProcessInfo() = delete;
  // Ensure move construction is noexcept.
  ChildProcessInfo(ChildProcessInfo&&) noexcept = default;
  ChildProcessInfo& operator=(ChildProcessInfo&&) noexcept = default;
  // Delete copy constructors.
  ChildProcessInfo(const ChildProcessInfo&) = delete;
  ChildProcessInfo& operator=(const ChildProcessInfo&) = delete;

  /// Get the Protobuf description of the child process.
  [[nodiscard]] const ProcessInfo& get_process_info() const noexcept;

  /// Set the state information.
  /// @param[in] state The new state of the child process.
  /// @param[in] core_dumped Whether or not the process core dumped.
  /// @param[in] why The process exit code.
  void set_state(ProcessState state, bool core_dumped, int32_t why) noexcept;

  /// Send a signal to the subprocess.
  void send_signal(int signal_value) const noexcept;

  /// Stop the subprocess.
  void stop_process();

  /// Start the child process.
  void start() noexcept;

  /// Get the name for the log file.
  [[nodiscard]] const filesystem::Path& get_log_file() const noexcept;

  /// Do any clean up associated with exiting the process.
  void handle_process_exit();

  /// Query if the process has exited (PROCESS_STATE_EXITED or PROCESS_STATE_CRASHED)
  [[nodiscard]] bool has_exited() const noexcept;

private:
  /// Calculate the name for the output file.
  [[nodiscard]] filesystem::Path create_output_file_name() const noexcept;

  /// The configuration for this child.
  AppConfig config_;
  /// The directory to place process log output files in.
  filesystem::Path logging_directory_;
  /// The file containing the log output.
  filesystem::Path log_file_;
  /// The memory resource to use.
  memory::MemoryResource memory_resource_;
  /// A pointer to the Boost asio context.
  memory::ObjectPtr<boost::asio::io_context> io_ctx_ptr_;
  /// Information for gRPC status reporting.
  ProcessInfo process_info_;
  /// Timer for process exits.
  std::optional<boost::asio::deadline_timer> maybe_timer_{};
};
} // namespace jewels::simplelaunch
