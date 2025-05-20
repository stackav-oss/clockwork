// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
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
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/system/errc.hpp>
#include <sched.h>

#include <cstdint>
#include <functional>
#include <memory_resource>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace jewels::simplelaunch
{

/// Implementation of the TaskManager gRPC service.
class TaskManagerImpl final
{
public:
  /// Constructor.
  /// @param[in] config The configuration to use.
  /// @param[in] logging_directory The directory used for process output.
  /// @param[in] memory_resource The memory resource to use.
  /// @param[in] io_ctx_ptr A pointer to an IO context.
  /// @param[in] pre_launch_task_results Information about pre_launch processes.
  TaskManagerImpl(
    Config config,
    jewels::filesystem::Path logging_directory,
    jewels::memory::MemoryResource memory_resource,
    jewels::memory::ObjectPtr<boost::asio::io_context> io_ctx_ptr,
    std::pmr::unordered_map<std::pmr::string, bool> pre_launch_task_results) noexcept;

  /// Create HTTP server.
  /// @param[in] http_endpoint The endpoint to run the HTTP server on.
  jewels::expected<void, boost::beast::error_code>
  create_http_server(const boost::asio::ip::tcp::endpoint& http_endpoint);

  /// Register POSIX signal handlers.
  void register_signal_handlers();

  /// Spawn the subprocesses specified in `config_`.
  void spawn_subprocesses();

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

  /// Register `accept_connection` callback.
  void register_accept_connection();

  /// Accept a TCP connection.
  void accept_connection(boost::beast::error_code error_code, boost::asio::ip::tcp::socket socket);

  /// Send same signal to all children, with an optional wait
  void send_signal_to_all(int signal_number, std::string_view signal_name, uint32_t wait_in_s = 0);

  /// The configuration of this service.
  Config config_;

  /// The directory to place process log output files in.
  jewels::filesystem::Path logging_directory_;

  /// The memory resource used by the service.
  jewels::memory::MemoryResource memory_resource_;

  /// An IO context pointer to use for listening to signals.
  jewels::memory::ObjectPtr<boost::asio::io_context> io_ctx_ptr_;

  /// Mutex to protect child_processes_.
  std::mutex child_processes_mutex_;

  /// Information about pre_launch processes.
  std::pmr::unordered_map<std::pmr::string, bool> pre_launch_task_results_;

  /// Information about child processes.
  std::pmr::unordered_map<std::pmr::string, ChildProcessInfo> child_processes_;

  /// The SIGCHLD signal set.
  boost::asio::signal_set sigchld_;
  /// A signal set for top level signals like SIGINT.
  boost::asio::signal_set quit_signals_;
  /// The TCP connect for the web server.
  boost::asio::ip::tcp::acceptor tcp_acceptor_;
};
} // namespace jewels::simplelaunch
