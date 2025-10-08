// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/offboard/async_work_queue.hh"

#include <cassert>
#include <cstddef>
#include <utility>
#include <vector>

namespace clockwork_logging::offboard
{

AsyncWorkQueue::AsyncWorkQueue(size_t num_worker_threads)
{
  assert(num_worker_threads != 0U);
  worker_threads_.reserve(num_worker_threads);
  for (size_t i = 0U; i < num_worker_threads; ++i)
  {
    worker_threads_.emplace_back([this]() { worker_thread_fn(); });
  }
}

AsyncWorkQueue::~AsyncWorkQueue()
{
  {
    const std::scoped_lock guard(work_queue_mutex_);
    shutdown_flag_ = true;
    worker_condvar_.notify_all();
  }
  for (auto& worker_thread : worker_threads_)
  {
    worker_thread.join();
  }
}

void AsyncWorkQueue::schedule_work_item(std::function<void()> work_item)
{
  std::unique_lock guard(work_queue_mutex_);
  scheduler_condvar_.wait(guard, [this] { return work_queue_.size() + active_worker_count_ < worker_threads_.size(); });
  work_queue_.emplace_back(std::move(work_item));
  worker_condvar_.notify_one();
}

void AsyncWorkQueue::schedule_work_item_no_wait(std::function<void()> work_item)
{
  const std::scoped_lock guard(work_queue_mutex_);
  work_queue_.emplace_back(std::move(work_item));
  worker_condvar_.notify_one();
}

void AsyncWorkQueue::worker_thread_fn()
{
  while (true)
  {
    std::function<void()> work_item;
    {
      std::unique_lock guard(work_queue_mutex_);
      worker_condvar_.wait(guard, [this] { return shutdown_flag_ || !work_queue_.empty(); });
      if (shutdown_flag_ && work_queue_.empty())
      {
        break;
      }
      work_item = std::move(work_queue_.front());
      work_queue_.pop_front();
      ++active_worker_count_;
    }
    work_item();
    const std::unique_lock guard(work_queue_mutex_);
    --active_worker_count_;
    scheduler_condvar_.notify_one();
  }
}

} // namespace clockwork_logging::offboard
