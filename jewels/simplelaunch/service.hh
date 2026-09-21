// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/simplelaunch_runner_config_clk_cc.hh"
#include "clockwork/diagnostics/report_definitions.hh"
#include "clockwork/diagnostics/reporter.hh"
#include "clockwork/pinion/abstract_channel.hh"
#include "clockwork/pinion/abstract_channel_factory.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/runners/epoll_manager.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/simplelaunch/child_process.hh"
#include "jewels/simplelaunch/config.hh"
#include "jewels/std/expected.hh"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/http/message_fwd.hpp>
#include <boost/beast/http/string_body_fwd.hpp>
#include <boost/system/errc.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <tuple>

namespace jewels::simplelaunch
{

/// Implementation of the TaskManager gRPC service.
class TaskManagerImpl final
{
public:
  /// Constructor.
  /// @param[in] config The configuration to use.
  /// @param[in] runner_config_ptr The simplelaunch runner configuration to use
  /// @param[in] logging_directory The directory used for process output.
  /// @param[in] memory_resource The memory resource to use.
  /// @param[in] io_ctx_ptr A pointer to an IO context.
  /// @param[in] pre_launch_task_results Information about pre_launch processes.
  TaskManagerImpl(
    Config config,
    std::shared_ptr<clockwork::Tappy<SimplelaunchRunnerConfig>> runner_config_ptr,
    jewels::filesystem::Path logging_directory,
    jewels::memory::MemoryResource memory_resource,
    jewels::memory::ObjectPtr<boost::asio::io_context> io_ctx_ptr,
    std::pmr::map<std::pmr::string, bool> pre_launch_task_results) noexcept;

  /// Initialize the task manager
  /// @param[in] channel_factory Channel factory used to create the status publisher
  /// @param[in] epoll EPoll manager
  /// @return True for success, false for failure
  [[nodiscard]] bool
  initialize(clockwork::pinion::AbstractChannelFactory& channel_factory, clockwork::EPollManager& epoll);

  /// Create HTTP server.
  /// @param[in] http_endpoint The endpoint to run the HTTP server on.
  jewels::expected<void, boost::beast::error_code>
  create_http_server(const boost::asio::ip::tcp::endpoint& http_endpoint);

  /// Register POSIX signal handlers.
  void register_signal_handlers();

  /// Spawn the subprocesses specified in `config_`.
  void spawn_subprocesses();

  /// Publish the simplelaunch runner status message
  void publish_status_message();

  /// Publish the simplelaunch runner diagnostics report
  void publish_diagnostics();

private:
  /// Get a list of processes.
  /// @param[in] binary_encoding Whether or not to use Protobuf's binary format in the response body.
  /// @param[in,out] response The response to populate.
  void get_process_list(bool binary_encoding, boost::beast::http::response<boost::beast::http::string_body>& response);

  /// Execute a process action.
  /// @param[in] request_body The body of the HTTP request - this should be a serialized `ProcessActionRequest`.
  /// @param[in,out] response The response to populate.
  void process_action(
    const std::string& request_body, boost::beast::http::response<boost::beast::http::string_body>& response);

  /// Get a child process by PID.
  /// @pre You must hold `child_processes_mutex_`.
  ChildProcessInfo* get_child_by_pid(pid_t pid);

  /// Process the result of waitpid.
  /// @param[in] The PID that has exited.
  /// @param[in] The status code it exited with.
  void process_waitpid(pid_t pid, int status);

  /// Process SIGCHLD.
  void sigchld_callback(const boost::system::error_code& error, int signal_number);
  /// Process top level signals like SIGINT.
  void quit_callback(const boost::system::error_code& error, int signal_number);

  /// Stop all child processes and exit.
  void quit();

  /// Register `accept_connection` callback.
  void register_accept_connection();

  /// Accept a TCP connection.
  void accept_connection(boost::beast::error_code error_code, boost::asio::ip::tcp::socket socket);

  /// Send same signal to all children, with an optional wait
  /// @return The number of children that were sent a signal.
  size_t send_signal_to_all(int signal_number, std::string_view signal_name, uint32_t wait_in_s = 0);

  /// Send signals to all children and escalate the signal if the child doesn't exit.
  void escalating_send_signal_to_all();

  /// The configuration of this service.
  Config config_;

  /// The clockwork simplelaunch runner configuration of this server
  std::shared_ptr<clockwork::Tappy<SimplelaunchRunnerConfig>> runner_config_ptr_;

  /// The directory to place process log output files in.
  jewels::filesystem::Path logging_directory_;

  /// The memory resource used by the service.
  jewels::memory::MemoryResource memory_resource_;

  /// Status publisher
  std::shared_ptr<clockwork::pinion::AbstractPublisher> status_publisher_;

  /// Status publisher handle
  std::optional<clockwork::pinion::PublisherHandle> maybe_status_publisher_handle_;

  /// Diagnostics publisher
  std::shared_ptr<clockwork::pinion::AbstractPublisher> diagnostics_publisher_;

  /// Diagnostics manager
  std::unique_ptr<clockwork::diagnostics::ClockworkManager<clockwork::diagnostics::SignalGroupId::simplelaunch>>
    diagnostics_manager_;

  /// An IO context pointer to use for listening to signals.
  jewels::memory::ObjectPtr<boost::asio::io_context> io_ctx_ptr_;

  /// Mutex to protect child_processes_.
  std::mutex child_processes_mutex_;

  /// Information about pre_launch processes.
  std::pmr::map<std::pmr::string, bool> pre_launch_task_results_;

  /// Information about child processes.
  std::pmr::map<std::pmr::string, ChildProcessInfo> child_processes_;

  /// The SIGCHLD signal set.
  boost::asio::signal_set sigchld_;
  /// A signal set for top level signals like SIGINT.
  boost::asio::signal_set quit_signals_;
  /// The TCP connect for the web server.
  boost::asio::ip::tcp::acceptor tcp_acceptor_;
};
} // namespace jewels::simplelaunch
