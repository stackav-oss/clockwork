// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/abstract_cog.hh"
#include "clockwork/common/abstract_cog_queue.hh"
#include "clockwork/common/cog_envelope.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/time/sync_time.hh"

#include <chrono>
#include <condition_variable>
#include <list>
#include <memory_resource>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

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
  using TimerUpdate = std::pair<jewels::memory::ObjectPtr<AbstractCog>, jewels::time::SyncTime>;
  using TimerUpdates = std::pmr::vector<TimerUpdate>;

  /// Scan the queue once for a ready Cog. Must be called with mutex_ held.
  std::optional<CogEnvelope> try_pop_ready(
    jewels::Out<std::optional<jewels::time::SyncTime>> earliest_throttle_deadline,
    jewels::Out<TimerUpdates> timer_updates);

  /// Arm publisher-throttle timers after releasing mutex_.
  static void arm_publisher_throttle_timers(const TimerUpdates& timer_updates);

  /// Memory resource for temporary queue-scan bookkeeping.
  jewels::memory::MemoryResource memory_resource_;

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
