// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/runners/online_runner.hh"

#include "clockwork/runners/thread_pool.hh"
#include "jewels/memory/pointers.hh"

#include <stdexcept>
#include <utility>

namespace clockwork
{

OnlineRunner::OnlineRunner(OnlineRunnerConfig config)
  : config_(std::move(config))
{
}

OnlineRunner::~OnlineRunner()
{
  stop();
  join();
}

void OnlineRunner::start()
{
  if (started_)
  {
    throw std::runtime_error("Attempted to call start on OnlineRunner twice.");
  }

  config_.pool->start();
  started_ = true;
}

void OnlineRunner::stop()
{
  config_.pool->stop();
}

void OnlineRunner::join()
{
  config_.pool->join();
}

} // namespace clockwork
