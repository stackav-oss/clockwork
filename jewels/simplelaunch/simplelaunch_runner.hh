// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/simplelaunch_runner_config_clk_cc.hh"
#include "clockwork/pinion/abstract_channel_factory.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/runners/epoll_manager.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/simplelaunch/service.hh"
#include "jewels/simplelaunch/v1/config.pb.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <memory_resource>
#include <string>

namespace jewels::simplelaunch
{

/// Simplelaunch runner class
class SimplelaunchRunner
{
public:
  struct CtorParams
  {
    /// Memory Resource
    jewels::memory::MemoryResource memory_resource;
    /// Simplelaunch configuration protobuf
    ::jewels::simplelaunch::v1::Config config;
    /// Simplelaunch runner config for clockwork integration
    std::shared_ptr<clockwork::Tappy<SimplelaunchRunnerConfig>> runner_config_ptr;
    /// Pinion channel factory
    std::shared_ptr<clockwork::pinion::AbstractChannelFactory> channel_factory_ptr;
    /// Pre-launch task results
    std::pmr::map<std::pmr::string, bool> pre_launch_results;
    /// Path to write the logs to
    jewels::filesystem::Path logging_directory;
    /// HTTP listen host
    std::string listen_host;
    /// HTTP listen port
    uint16_t listen_port;
  };

  /// Constructor
  explicit SimplelaunchRunner(const CtorParams& params);

  ~SimplelaunchRunner() = default;

  SimplelaunchRunner(const SimplelaunchRunner&) = delete;
  SimplelaunchRunner(SimplelaunchRunner&&) = delete;
  SimplelaunchRunner& operator=(const SimplelaunchRunner&) = delete;
  SimplelaunchRunner& operator=(SimplelaunchRunner&&) = delete;

  /// Initialize the simplelaunch runner
  /// @return Success or failure
  BinaryOutcome initialize();

  /// Run the simplelaunch runner.
  void run();

  /// Run the simplelaunch runner for the specified interval
  /// @param[in] interval Run interval
  void run_for(std::chrono::nanoseconds interval);

private:
  /// The io_context is required for all I/O
  boost::asio::io_context io_context_;

  /// TCP server endpoint
  boost::asio::ip::tcp::endpoint endpoint_;

  /// Task manager implementation
  TaskManagerImpl task_manager_;

  /// EPoll mananger for clockwork integration
  clockwork::EPollManager epoll_;

  /// Pinion channel factory
  std::shared_ptr<clockwork::pinion::AbstractChannelFactory> channel_factory_ptr_;

  /// Flag indicating that startup succeeded
  bool start_up_succeeded_;

  /// HTTP listen host
  std::string listen_host_;

  /// HTTP listen port
  uint16_t listen_port_;
};

} // namespace jewels::simplelaunch
