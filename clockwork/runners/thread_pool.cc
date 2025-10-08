// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/runners/thread_pool.hh"

#include "clockwork/common/abstract_cog.hh"
#include "clockwork/common/abstract_cog_queue.hh"
#include "clockwork/common/cog_envelope.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"

#include <cstddef>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace clockwork
{

ThreadPool::ThreadPool(ThreadPoolConfig config)
  : config_(std::move(config)), threads_(config_.resource)
{
}

ThreadPool::~ThreadPool()
{
  stop();
  join();
}

void ThreadPool::start()
{
  if (started_)
  {
    throw std::runtime_error("Attempted to call start on a thread pool twice");
  }

  threads_.resize(config_.thread_configs.size());
  for (size_t i = 0; i < config_.thread_configs.size(); ++i)
  {
    auto cfg = config_.thread_configs[i];
    threads_[i] = std::thread([this, cfg = std::move(cfg)]() { thread_body(cfg); });
  }

  started_ = true;
}

void ThreadPool::stop()
{
  shutdown_requested_ = true;
}

void ThreadPool::join()
{
  stop();

  for (auto& thread : threads_)
  {
    if (thread.joinable())
    {
      thread.join();
    }
  }
}

void ThreadPool::thread_body(const ThreadConfig& config)
{
  std::visit([this](const auto& work) { thread_body_impl(work); }, config.work);
}

void ThreadPool::thread_body_impl(jewels::memory::ObjectPtr<AbstractCogQueue> queue)
{
  while (!shutdown_requested_)
  {
    if (auto env = queue->pop(thread_loop_timeout); env)
    {
      auto params = CogExecuteParams{.start_time = jewels::time::SyncClock::now()};
      // The return value is ignored because all errors are already reported within execute() to the extent that we can
      // report them at present. Some of this reporting/handling is being improved in OI-2593, and more will be done in
      // OI-2730 when we have an improved observability framework.
      std::ignore = env->cog->execute(params); // TODO(OI-2730): Report errors from execute via observability
    }
  }
}

void ThreadPool::thread_body_impl(jewels::memory::ObjectPtr<AbstractEPollManager> epoll)
{
  while (!shutdown_requested_)
  {
    auto result = epoll->wait(thread_loop_timeout);
    if (!result)
    {
      jewels::log_cerr_error("Failed to wait for epoll event: {}", result.error());
    }
  }
}

} // namespace clockwork
