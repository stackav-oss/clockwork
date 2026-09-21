// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/simplelaunch/simplelaunch_runner.hh"

#include "jewels/filesystem/error_code.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"

#include <boost/asio/ip/address.hpp>
#include <boost/system/errc.hpp>

#include <algorithm>
#include <cerrno>
#include <compare>
#include <future>
#include <memory>
#include <string>

namespace jewels::simplelaunch
{

SimplelaunchRunner::SimplelaunchRunner(const CtorParams& params)
  : io_context_(1),
    endpoint_(boost::asio::ip::make_address(params.listen_host), params.listen_port),
    task_manager_(
      params.config,
      params.runner_config_ptr,
      params.logging_directory,
      params.memory_resource,
      memory::make_non_null_from_ref(io_context_),
      params.pre_launch_results),
    epoll_(params.memory_resource),
    channel_factory_ptr_(params.channel_factory_ptr),
    start_up_succeeded_(
      std::all_of(
        params.pre_launch_results.begin(),
        params.pre_launch_results.end(),
        [](auto& entry) -> bool { return entry.second; })),
    listen_host_(params.listen_host),
    listen_port_(params.listen_port)
{
}

BinaryOutcome SimplelaunchRunner::initialize()
{
  if (!task_manager_.initialize(*channel_factory_ptr_, epoll_))
  {
    return failure;
  }

  // Create the HTTP server
  if (const auto http_server_expected = task_manager_.create_http_server(endpoint_); !http_server_expected)
  {
    jewels::log_cerr_fatal("Failed to create http server: {}", http_server_expected.error().message());
    return failure;
  }

  jewels::log_cerr_info("Server listening on http://{}:{}", listen_host_, listen_port_);
  task_manager_.register_signal_handlers();
  if (start_up_succeeded_)
  {
    task_manager_.spawn_subprocesses();
  }

  return success;
}

void SimplelaunchRunner::run()
{
  while (!io_context_.stopped())
  {
    run_for(std::chrono::seconds(1));
  }
}

void SimplelaunchRunner::run_for(std::chrono::nanoseconds interval)
{
  auto run_future = std::async(
    std::launch::async,
    [this, interval]() { io_context_.run_for(boost::asio::chrono::nanoseconds(interval.count())); });
  auto now = time::SteadyClock::now();
  const auto wakeup_time = now + interval;
  while (now < wakeup_time)
  {
    if (const auto result = epoll_.wait(std::chrono::duration_cast<std::chrono::milliseconds>(wakeup_time - now));
        !result && result.error().value() != EINTR)
    {
      jewels::log_cerr_error("epoll wait failed with {}", result.error());
    }
    now = time::SteadyClock::now();
  }
  run_future.wait();
  task_manager_.publish_status_message();
  task_manager_.publish_diagnostics();
}

} // namespace jewels::simplelaunch
