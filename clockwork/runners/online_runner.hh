// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/abstract_cog.hh"
#include "clockwork/runners/thread_pool.hh"
#include "jewels/memory/pointers.hh"

#include <memory_resource>
#include <vector>

namespace clockwork
{

///
/// Configuration for the online runner.
///
struct OnlineRunnerConfig
{
  ///
  /// The cogs the runner should execute.
  ///
  std::pmr::vector<CogConfig> cogs;

  ///
  /// The thread pool to use for cog execution.
  ///
  jewels::memory::ObjectPtr<ThreadPool> pool;
};

class OnlineRunner
{
public:
  ///
  /// Constructor.
  /// @param[in] config The configuration.
  ///
  explicit OnlineRunner(OnlineRunnerConfig config);

  ///
  /// Destructor.
  ///
  ~OnlineRunner();

  OnlineRunner(const OnlineRunner&) = delete;
  OnlineRunner& operator=(const OnlineRunner&) = delete;
  OnlineRunner(OnlineRunner&&) = delete;
  OnlineRunner& operator=(OnlineRunner&&) = delete;

  ///
  /// Start the runner.
  ///
  void start();

  ///
  /// Stop the runner.
  ///
  void stop();

  ///
  /// Wait for the runner to complete (stops the pool if needed).
  ///
  void join();

private:
  ///
  /// The configuration.
  ///

  OnlineRunnerConfig config_;

  ///
  /// Flag indicating if the runner has been started.
  ///

  bool started_ = false;
};

} // namespace clockwork
