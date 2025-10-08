// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/simplelaunch/service.hh"

#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/simplelaunch/child_process.hh"
#include "jewels/simplelaunch/config.hh"
#include "jewels/simplelaunch/service_definition.hh"

#include <absl/status/status.h>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/impl/buffers_generator.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/error.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/impl/message_generator.hpp>
#include <boost/beast/http/message_generator.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/system/system_category.hpp>
#include <fmt10/format.h>
#include <google/protobuf/json/json.h>
#include <google/protobuf/repeated_ptr_field.h>
#include <google/protobuf/util/json_util.h>

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <optional>
#include <ranges>
#include <set>
#include <string_view>
#include <sys/wait.h>
#include <utility>

namespace jewels::simplelaunch
{

namespace
{
/// The MIME type to use for Protobuf.
constexpr auto protobuf_mime_type = "application/x-protobuf";

struct RequestContext
{
  explicit RequestContext(boost::asio::ip::tcp::socket socket)
    : stream{std::move(socket)}
  {
  }

  RequestContext() = delete;
  ~RequestContext() = default;
  // Disallow copying and moving
  RequestContext(RequestContext&&) noexcept = delete;
  RequestContext& operator=(RequestContext&&) noexcept = delete;
  RequestContext(const RequestContext&) = delete;
  RequestContext& operator=(const RequestContext&) = delete;

  boost::beast::tcp_stream stream;
  boost::beast::flat_buffer buffer{};
  boost::beast::http::request<boost::beast::http::string_body> request{};

  void close()
  {
    boost::beast::error_code error_code;
    // NOLINTNEXTLINE(bugprone-unused-return-value,cert-err33-c) - false positive; this is a void function.
    stream.socket().shutdown(boost::asio::ip::tcp::socket::shutdown_send, error_code);
    if (error_code)
    {
      log_cerr_error("Error closing connection: {}", error_code.message());
    }
  }

  void send_response(boost::beast::http::response<boost::beast::http::string_body> response)
  {
    response.version(request.version());
    response.prepare_payload();

    boost::beast::async_write(
      stream,
      boost::beast::http::message_generator(std::move(response)),
      [this](boost::beast::error_code error_code, std::size_t bytes_transferred)
      {
        log_cerr_debug("Read {} bytes", bytes_transferred);
        if (error_code)
        {
          log_cerr_error("Error responding: {}", error_code.message());
          return;
        }

        // If keep-alive is not enabled close the socket
        if (!request.keep_alive())
        {
          this->close();
        }
      });
  }

  void blocking_send_response(boost::beast::http::response<boost::beast::http::string_body> response)
  {
    response.version(request.version());
    response.prepare_payload();

    boost::beast::error_code error_code{};
    boost::beast::write(stream, boost::beast::http::message_generator(std::move(response)), error_code);

    if (error_code)
    {
      log_cerr_error("Error responding: {}", error_code.message());
    }

    // If keep-alive is not enabled close the socket
    if (!request.keep_alive())
    {
      this->close();
    }
  }
};

/// Get the output logs of a process.
/// @param[in] process the child process to retrieve the logs from.
/// @param[in,out] response The response to populate.
void get_logs(const ChildProcessInfo& process, boost::beast::http::response<boost::beast::http::string_body>& response)
{
  const auto& log_file_path = process.get_log_file();
  if (log_file_path.empty())
  {
    response.result(boost::beast::http::status::not_found);
    response.body() = "Process has no log file";
    return;
  };

  std::ifstream file_stream{log_file_path.c_str()};
  if (!file_stream.is_open())
  {
    response.result(boost::beast::http::status::not_found);
    response.body() = "Could not open process log file";
    return;
  }

  file_stream.seekg(0, std::ios::end);
  const auto file_size = file_stream.tellg();

  response.body().resize(static_cast<size_t>(file_size));

  file_stream.seekg(0);
  file_stream.read(response.body().data(), file_size);

  response.result(boost::beast::http::status::ok);
  response.set(boost::beast::http::field::content_type, "text/plain");
}
} // namespace

TaskManagerImpl::TaskManagerImpl(
  Config config,
  filesystem::Path logging_directory,
  memory::MemoryResource memory_resource,
  memory::ObjectPtr<boost::asio::io_context> io_ctx_ptr,
  std::pmr::unordered_map<std::pmr::string, bool> pre_launch_task_results) noexcept
  : config_(std::move(config)),
    logging_directory_(std::move(logging_directory)),
    memory_resource_{std::move(memory_resource)},
    io_ctx_ptr_{io_ctx_ptr},
    pre_launch_task_results_{std::move(pre_launch_task_results)},
    child_processes_{memory_resource_},
    sigchld_{*io_ctx_ptr_, SIGCHLD},
    quit_signals_{*io_ctx_ptr_, SIGINT, SIGQUIT, SIGTERM},
    tcp_acceptor_{boost::asio::make_strand(*io_ctx_ptr_)}
{
  child_processes_.reserve(static_cast<size_t>(config_.app_size()));
}

void TaskManagerImpl::get_process_list(
  bool binary_encoding, boost::beast::http::response<boost::beast::http::string_body>& response)
{
  GetProcessListResponse process_list;
  const std::scoped_lock guard{child_processes_mutex_};
  for (const auto& process : std::ranges::views::values(child_processes_))
  {
    *process_list.mutable_process_info()->Add() = process.get_process_info();
  }
  for (const auto& [task_name, task_succeeded] : pre_launch_task_results_)
  {
    PreLaunchInfo pre_launch_info;
    pre_launch_info.set_name(task_name);
    pre_launch_info.set_succeeded(task_succeeded);
    *process_list.mutable_pre_launch_info()->Add() = std::move(pre_launch_info);
  }

  response.result(boost::beast::http::status::ok);
  if (binary_encoding)
  {
    response.set(boost::beast::http::field::content_type, protobuf_mime_type);
    response.body() = process_list.SerializeAsString();
  }
  else
  {
    response.set(boost::beast::http::field::content_type, "application/json");
    google::protobuf::util::JsonPrintOptions json_options;
    json_options.add_whitespace = true;
    if (!google::protobuf::util::MessageToJsonString(process_list, &response.body(), json_options).ok())
    {
      response.result(boost::beast::http::status::internal_server_error);
      response.body() = "Could not serialize process list to JSON";
    }
  }
}

void TaskManagerImpl::process_action(
  const std::string& request_body, boost::beast::http::response<boost::beast::http::string_body>& response)
{
  SimpleLaunchCommand command{};
  if (!command.ParseFromString(request_body))
  {
    response.result(boost::beast::http::status::bad_request);
    response.body() = "Could not parse SimpleLaunchCommand from request body";
    return;
  }

  std::pmr::string app_name{memory_resource_};
  if (command.has_start_process())
  {
    app_name = command.start_process();

    auto child_process_iter = child_processes_.find(app_name);
    if (child_process_iter == child_processes_.end())
    {
      response.result(boost::beast::http::status::not_found);
      return;
    }
    if (static_cast<ProcessState>(child_process_iter->second.get_process_info().state()) == ProcessState::running)
    {
      response.result(boost::beast::http::status::bad_request);
      response.body() = fmt::format("'{}' is currently running", app_name);
    }
    else
    {
      child_process_iter->second.start();
    }
  }
  else if (command.has_stop_process())
  {
    app_name = command.stop_process();

    auto child_process_iter = child_processes_.find(app_name);
    if (child_process_iter == child_processes_.end())
    {
      response.result(boost::beast::http::status::not_found);
      return;
    }

    child_process_iter->second.stop_process();
  }
  else if (command.has_get_logs())
  {
    app_name = command.get_logs();

    auto child_process_iter = child_processes_.find(app_name);
    if (child_process_iter == child_processes_.end())
    {
      response.result(boost::beast::http::status::not_found);
      return;
    }

    get_logs(child_process_iter->second, response);
  }
  else if (command.has_stop_all_processes())
  {
    for (auto& child_process : std::ranges::views::values(child_processes_))
    {
      if (child_process.get_process_info().state() == static_cast<int>(ProcessState::running))
      {
        child_process.stop_process();
      }
    }
  }
  else
  {
    response.result(boost::beast::http::status::bad_request);
  }
}

ChildProcessInfo* TaskManagerImpl::get_child_by_pid(pid_t pid)
{
  for (auto& child_process : std::ranges::views::values(child_processes_))
  {
    if (child_process.get_process_info().pid() == pid)
    {
      return &child_process;
    }
  }
  return nullptr;
}

void TaskManagerImpl::process_waitpid(pid_t pid, int status)
{
  log_cerr_debug("PID {}, status {}", pid, status);

  auto* child_ptr = this->get_child_by_pid(pid);
  if (child_ptr == nullptr)
  {
    log_cerr_error("PID {} isn't a known child", pid);
    return;
  }

  std::optional<ProcessState> new_state;
  if (WIFEXITED(status))
  {
    child_ptr->handle_process_exit(); // Cancel any active timers on the process
    new_state = ProcessState::exited;
  }
  else if (WIFSIGNALED(status))
  {
    child_ptr->handle_process_exit(); // Cancel any active timers on the process
    new_state = ProcessState::crashed;
  }

  if (new_state)
  {
    child_ptr->set_state(*new_state, WCOREDUMP(status), WEXITSTATUS(status));
  }
}

void TaskManagerImpl::sigchld_callback(const boost::system::error_code& error, int /*signal_number*/)
{
  // Re-register the sigchld handler so it is called for the next signal
  sigchld_.async_wait([this](boost::system::error_code next_error, int next_signal)
                      { this->sigchld_callback(next_error, next_signal); });

  if (error)
  {
    log_cerr_error("Error passed to SIGCHLD handler: {}", error.message());
    return;
  }

  log_cerr_debug("Got SIGCHLD");

  const std::scoped_lock guard{child_processes_mutex_};
  pid_t pid = 0;
  int status = 0;
  while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
  {
    process_waitpid(pid, status);
  }
}

size_t TaskManagerImpl::send_signal_to_all(int signal_number, std::string_view signal_name, uint32_t wait_in_s)
{
  if (wait_in_s > 0)
  {
    struct timespec req{.tv_sec = wait_in_s, .tv_nsec = 0};
    struct timespec rem{};
    while (nanosleep(&req, &rem) == -1 && errno == EINTR)
    {
      // If interrupted by a signal, update req with remaining time
      req = rem;
    }
  }
  const std::scoped_lock guard{child_processes_mutex_};
  // Cleaning up children that may have died without signaling
  pid_t pid = 0;
  int status = 0;
  while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
  {
    process_waitpid(pid, status);
  }
  uint32_t non_exited = 0U;
  uint32_t exited = 0U;
  for (auto& child_process : std::ranges::views::values(this->child_processes_))
  {
    if (!child_process.has_exited())
    {
      child_process.send_signal(signal_number);
      non_exited++;
    }
    else
    {
      exited++;
    }
  }
  log_cerr_info("Sent SIG{} to {} children, ignoring {} already exited", signal_name, non_exited, exited);
  return non_exited;
}

void TaskManagerImpl::quit_callback(const boost::system::error_code& error, int /* signal_number */)
{
  if (error)
  {
    log_cerr_error("Error passed to signal handler: {}", error.message());
    // Re-register the signal handler
    quit_signals_.async_wait([this](boost::system::error_code next_error, int next_signal)
                             { this->quit_callback(next_error, next_signal); });
    return;
  }

  quit();
}

void TaskManagerImpl::escalating_send_signal_to_all()
{
  constexpr auto sleep_timeout = 3;
  if (send_signal_to_all(SIGINT, "INT") == 0U)
  {
    return;
  }
  if (send_signal_to_all(SIGTERM, "TERM", sleep_timeout) == 0U)
  {
    return;
  }
  if (send_signal_to_all(SIGABRT, "ABRT", sleep_timeout) == 0U)
  {
    return;
  }
  send_signal_to_all(SIGKILL, "KILL", sleep_timeout);
}

void TaskManagerImpl::quit()
{
  tcp_acceptor_.close();
  escalating_send_signal_to_all();
  io_ctx_ptr_->stop();
}

expected<void, boost::beast::error_code>
TaskManagerImpl::create_http_server(const boost::asio::ip::tcp::endpoint& http_endpoint)
{
  boost::beast::error_code error_code{};

  // NOLINTNEXTLINE(bugprone-unused-return-value,cert-err33-c) - false positive; this is a void function.
  tcp_acceptor_.open(http_endpoint.protocol(), error_code);
  if (error_code)
  {
    return unexpected(error_code);
  }

  // Allow address reuse
  // NOLINTNEXTLINE(bugprone-unused-return-value,cert-err33-c) - false positive; this is a void function.
  tcp_acceptor_.set_option(boost::asio::socket_base::reuse_address(true), error_code);
  if (error_code)
  {
    return unexpected(error_code);
  }

  // Set FD_CLOEXEC so that the file descriptor for the listen socket is closed when the subprocess execs.
  const auto tcp_fd = tcp_acceptor_.native_handle();
  // fcntl(2) returns bit flags for F_GETFD
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-signed-bitwise)
  if (::fcntl(tcp_fd, F_SETFD, fcntl(tcp_fd, F_GETFD) | FD_CLOEXEC) == -1)
  {
    return unexpected(boost::system::error_code{errno, boost::system::system_category()});
  }

  // Bind to the server address
  // NOLINTNEXTLINE(bugprone-unused-return-value,cert-err33-c) - false positive; this is a void function.
  tcp_acceptor_.bind(http_endpoint, error_code);
  if (error_code)
  {
    return unexpected(error_code);
  }

  // Start listening for connections
  // NOLINTNEXTLINE(bugprone-unused-return-value,cert-err33-c) - false positive; this is a void function.
  tcp_acceptor_.listen(boost::asio::socket_base::max_listen_connections, error_code);
  if (error_code)
  {
    return unexpected(error_code);
  }

  register_accept_connection();

  return {};
}

void TaskManagerImpl::register_accept_connection()
{
  tcp_acceptor_.async_accept(
    boost::asio::make_strand(*io_ctx_ptr_),
    [this](auto accept_error_code, boost::asio::ip::tcp::socket socket)
    { this->accept_connection(accept_error_code, std::move(socket)); });
}

void TaskManagerImpl::accept_connection(boost::beast::error_code error_code, boost::asio::ip::tcp::socket socket)
{
  if (error_code)
  {
    log_cerr_info("Error accepting connection: {}", error_code.message());
    return;
  }

  // Set FD_CLOEXEC so that the file descriptor for the socket is closed when the subprocess execs.
  const auto socket_fd = socket.native_handle();
  // fcntl(2) returns bit flags for F_GETFD
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-signed-bitwise)
  if (::fcntl(socket_fd, F_SETFD, fcntl(socket_fd, F_GETFD) | FD_CLOEXEC) == -1)
  {
    log_cerr_error(
      "Error accepting connection: {}", boost::system::error_code{errno, boost::system::system_category()}.message());
    return;
  }

  auto request_ctx_ptr = memory::allocate_shared<RequestContext, std::pmr::polymorphic_allocator<RequestContext>>(
    memory_resource_, std::move(socket));

  // Read a request
  boost::beast::http::async_read(
    request_ctx_ptr->stream,
    request_ctx_ptr->buffer,
    request_ctx_ptr->request,
    [this, request_ctx_ptr](boost::beast::error_code read_error_code, std::size_t bytes_transferred)
    {
      log_cerr_debug("Read {} bytes", bytes_transferred);

      // This means they closed the connection
      if (read_error_code == boost::beast::http::error::end_of_stream)
      {
        log_cerr_debug("Closing connection");
        request_ctx_ptr->close();
        return;
      }

      if (read_error_code)
      {
        log_cerr_error("Error reading request: {}", read_error_code.message());
        return;
      }

      const auto method = request_ctx_ptr->request.method();
      const auto path = std::string_view{request_ctx_ptr->request.target()};
      const bool accepts_binary =
        std::string_view{request_ctx_ptr->request[boost::beast::http::field::accept]} == protobuf_mime_type;

      boost::beast::http::response<boost::beast::http::string_body> response;
      response.set(boost::beast::http::field::access_control_allow_origin, "*");
      response.set(boost::beast::http::field::access_control_allow_methods, "*");
      response.set(boost::beast::http::field::access_control_allow_headers, "*");

      if (path == "/quit" && method == boost::beast::http::verb::post)
      {
        quit();
        // Use a blocking write here to make sure the response is sent
        // so the client knows it was acknowledged.
        request_ctx_ptr->blocking_send_response(std::move(response));
        return;
      }

      if (path == "/" && method == boost::beast::http::verb::get)
      {
        get_process_list(accepts_binary, response);
      }
      else if (path == "/" && method == boost::beast::http::verb::post)
      {
        process_action(request_ctx_ptr->request.body(), response);
      }
      else if (method != boost::beast::http::verb::options)
      {
        response.set(boost::beast::http::field::content_type, "text/plain");
        response.result(boost::beast::http::status::bad_request);
        response.body() = fmt::format("Invalid {} request", std::string_view{boost::beast::http::to_string(method)});
      }
      request_ctx_ptr->send_response(std::move(response));
    });

  // Accept more connections
  register_accept_connection();
}

void TaskManagerImpl::register_signal_handlers()
{
  // Register signal handlers
  sigchld_.async_wait([this](boost::system::error_code error, int signal) { this->sigchld_callback(error, signal); });
  quit_signals_.async_wait([this](boost::system::error_code error, int signal) { this->quit_callback(error, signal); });
}

void TaskManagerImpl::spawn_subprocesses()
{
  // Now let's kick off all of the currently configured children
  const std::scoped_lock guard{child_processes_mutex_};

  // First create all the child process descriptions
  for (const auto& app_config : config_.app())
  {
    std::pmr::string app_name{memory_resource_};
    app_name = app_config.name();
    const auto [_, did_insert] =
      child_processes_.try_emplace(app_name, app_config, logging_directory_, memory_resource_, io_ctx_ptr_);

    if (!did_insert)
    {
      log_cerr_fatal("Duplicate app name {}", app_config.name());
      return;
    }
  }

  // Then launch them
  for (auto& child_process : std::ranges::views::values(child_processes_))
  {
    child_process.start();
  }
}

} // namespace jewels::simplelaunch
