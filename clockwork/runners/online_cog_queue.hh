// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/abstract_cog_queue.hh"
#include "clockwork/common/cog_envelope.hh"
#include "jewels/memory/memory_resource.hh"

#include <chrono>
#include <condition_variable>
#include <list>
#include <memory_resource>
#include <mutex>

namespace clockwork
{

///
/// Simple FIFO queue implementation that uses a mutex for thread safety.
/// Assumes that Cogs are not reentrant and only allows one instance per Cog in the queue.
///
class OnlineCogQueue : public AbstractCogQueue
{
public:
  using PopResult = typename AbstractCogQueue::PopResult;

  ///
  /// Constructor.
  ///
  explicit OnlineCogQueue(jewels::memory::MemoryResource resource);

  ///
  /// Destructor.
  ///
  ~OnlineCogQueue() override;

  OnlineCogQueue(const OnlineCogQueue&) = delete;
  OnlineCogQueue& operator=(const OnlineCogQueue&) = delete;
  OnlineCogQueue(OnlineCogQueue&&) = delete;
  OnlineCogQueue& operator=(OnlineCogQueue&&) = delete;

  ///
  /// Notify the queue that the elements on the queue changed.
  /// This is primarily used when cogs complete execution to allow the
  /// queues to re-check cogs on the queue for updated readiness (i.e. shared state
  /// is now available).
  ///
  void notify() override;

  ///
  /// Attempt to push a Cog onto the queue. If there is already
  /// an envelope for this cog in the queue then do nothing.
  /// @param[in] envelope The cog envelope to push onto the queue.
  ///
  void push(CogEnvelope envelope) override;

  ///
  /// Pop the next available Cog off of the queue.
  /// @param[in] timeout The max time to wait for new data.
  /// @returns The next item on the queue if any.
  ///
  PopResult pop(std::chrono::nanoseconds timeout) override;

  ///
  /// Get the queue statistics.
  ///
  CogQueueStats stats() const override;

  ///
  /// Check if this cog queue is offline.
  /// @returns true if this cog queue is running offline.
  ///
  [[nodiscard]] bool is_offline() const override;

private:
  ///
  /// Mutex used for access to the queue.
  ///

  mutable std::mutex mutex_;

  ///
  /// Condition variable used to notify on new data.
  ///

  std::condition_variable condition_variable_;

  ///
  /// The cog queue.
  ///

  std::pmr::list<CogEnvelope> queue_;
};

} // namespace clockwork
