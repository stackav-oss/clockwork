// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <list>
#include <mutex>
#include <thread>
#include <vector>

namespace clockwork_logging::offboard
{

/// Async work queue for reading and writing to S3
///
/// NOTE: This code is for offboard use only. Do not run it on the vehicle
class AsyncWorkQueue
{
public:
  /// Constructor starts the worker threads
  /// @param[in] num_worker_threads Number of worker threads
  explicit AsyncWorkQueue(size_t num_worker_threads);

  /// Destructor shuts down the worker threads
  ~AsyncWorkQueue();

  AsyncWorkQueue(const AsyncWorkQueue&) = delete;
  AsyncWorkQueue& operator=(const AsyncWorkQueue&) = delete;
  AsyncWorkQueue(AsyncWorkQueue&&) = delete;
  AsyncWorkQueue& operator=(AsyncWorkQueue&&) = delete;

  /// Schedule a work item for processing, block until a worker thread is available
  /// @param[in] work_item Work item to schedule
  void schedule_work_item(std::function<void()> work_item);

  /// Schedule a work item for processing without blocking
  /// Blocks until a worker thread is available
  /// @param[in] work_item Work item to schedule
  void schedule_work_item_no_wait(std::function<void()> work_item);

private:
  /// Worker thread function
  void worker_thread_fn();

  /// Worker threads
  std::vector<std::thread> worker_threads_;

  /// Flag set when the queue is shutting down
  bool shutdown_flag_{false};

  /// Mutex protecting the work queue
  std::mutex work_queue_mutex_;

  /// Condition variable to signal the worker thread
  std::condition_variable worker_condvar_;

  /// Condition variable to signal waiting schedulers
  std::condition_variable scheduler_condvar_;

  /// Work queue
  std::list<std::function<void()>> work_queue_;

  /// Number of active worker threads
  size_t active_worker_count_{0U};
};

} // namespace clockwork_logging::offboard
