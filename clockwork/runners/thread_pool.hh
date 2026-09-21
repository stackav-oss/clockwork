// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/abstract_cog_queue.hh"
#include "clockwork/common/abstract_epoll_manager.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"

#include <atomic>
#include <chrono>
#include <memory_resource>
#include <thread>
#include <variant>
#include <vector>

namespace clockwork
{

///
/// Thread configuration. This will include priority, scheduling
/// policy, cpu affinity etc in the future. For now its just the
/// work queue the thread should get cogs from.
///
struct ThreadConfig
{
  ///
  /// The work for this thread.  One one:
  /// - cog queue to process
  /// - epoll manager to dispatch
  ///

  std::variant<jewels::memory::ObjectPtr<AbstractCogQueue>, jewels::memory::ObjectPtr<AbstractEPollManager>> work;
};

///
/// Thread pool configuration.
///
struct ThreadPoolConfig
{
  ///
  /// Memory resource for allocating thread storage.
  ///
  jewels::memory::MemoryResource resource;

  ///
  /// The thread configurations.
  ///
  std::pmr::vector<ThreadConfig> thread_configs;
};

///
/// Thread pool.
///
/// @note this class is not thread safe.
///
class ThreadPool
{
public:
  ///
  /// Constructor.
  ///
  /// @param[in] config Thread configurations, the pool will contain one thread
  /// per configuration.
  ///
  explicit ThreadPool(ThreadPoolConfig config);

  ///
  /// Destructor.
  ///
  /// Stops the thread pool and joins all threads if needed.
  ///
  ~ThreadPool();

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;
  ThreadPool(ThreadPool&&) = delete;
  ThreadPool& operator=(ThreadPool&&) = delete;

  ///
  /// Start the thread pool.
  ///
  void start();

  ///
  /// Stop the thread pool.
  ///
  void stop();

  ///
  /// Wait for all threads to complete (stops the pool if needed).
  ///
  void join();

private:
  static constexpr auto thread_loop_timeout = std::chrono::milliseconds(5);

  ///
  /// Flag indicating if the thread should stop.
  ///

  std::atomic<bool> shutdown_requested_ = false;

  ///
  /// Config.
  ///

  ThreadPoolConfig config_;

  ///
  /// Flag indicating if the pool has been started.
  ///

  bool started_ = false;

  ///
  /// The threads managed by this pool.
  ///

  std::pmr::vector<std::thread> threads_;

  ///
  /// Thread body definition for pool threads.
  /// @param[in] config The configuration for this thread.
  ///
  void thread_body(const ThreadConfig& config);

  void thread_body_impl(jewels::memory::ObjectPtr<AbstractCogQueue> queue);
  void thread_body_impl(jewels::memory::ObjectPtr<AbstractEPollManager> epoll);
};

} // namespace clockwork
