// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/abstract_cog_queue.hh"
#include "clockwork/common/cog_envelope.hh"
#include "clockwork/common/forward.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/time/sync_time.hh"

#include <chrono>
#include <list>
#include <memory_resource>

namespace clockwork
{

///
/// A simple FIFO queue for use in the deterministic runner. Not thread-safe.
///
class DeterministicCogQueue : public AbstractCogQueue
{
public:
  using PopResult = typename AbstractCogQueue::PopResult;

  ///
  /// Constructor.
  ///
  explicit DeterministicCogQueue(jewels::memory::MemoryResource resource);

  ///
  /// Destructor.
  ///
  ~DeterministicCogQueue() override = default;

  DeterministicCogQueue(const DeterministicCogQueue&) = delete;
  DeterministicCogQueue& operator=(const DeterministicCogQueue&) = delete;
  DeterministicCogQueue(DeterministicCogQueue&&) = delete;
  DeterministicCogQueue& operator=(DeterministicCogQueue&&) = delete;

  ///
  /// Notify the queue that the elements on the queue changed.
  /// This is primarily used when cogs complete execution to allow the
  /// queues to re-check cogs on the queue for updated readiness (i.e. shared state
  /// is now available).
  ///
  void notify() override;

  ///
  /// Attempt to push a Cog onto the queue. If there is already
  /// an envelope for this cog in the queue, retain the earliest ready time.
  /// @param[in] envelope The cog envelope to push onto the queue.
  ///
  void push(CogEnvelope envelope) override;

  ///
  /// Pop the next available Cog off of the queue.
  /// @param[in] timeout Ignored
  /// @returns The next item on the queue if any.
  ///
  PopResult pop(std::chrono::nanoseconds /*timeout*/) override;

  ///
  /// Returns the next available Cog off of the queue without removing it.
  /// @returns The next item on the queue if any.
  ///
  PopResult peek();

  /// Remove the envelope that would be returned by peek().
  void remove_next();

  /// Update the publisher throttle deadline for a queued Cog.
  /// @param[in] cog The throttled Cog.
  /// @param[in] throttled_until The next eligibility deadline.
  void set_throttled_until(jewels::memory::ObjectPtr<AbstractCog> cog, jewels::time::SyncTime throttled_until);
  ///
  /// Get the queue statistics.
  ///
  [[nodiscard]] CogQueueStats stats() const override;

  ///
  /// Check if this cog queue is offline.
  /// @returns true if this cog queue is running offline.
  ///
  [[nodiscard]] bool is_offline() const override;

private:
  using Queue = std::pmr::list<CogEnvelope>;
  using QueueIterator = Queue::iterator;

  /// Find the envelope that is eligible first after publisher throttling.
  QueueIterator find_next();

  Queue queue_;
};

} // namespace clockwork
