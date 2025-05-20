// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/abstract_cog_queue.hh"
#include "clockwork/common/cog_envelope.hh"
#include "jewels/memory/memory_resource.hh"

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
  /// an envelope for this cog in the queue then do nothing.
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
  ///
  /// Get the queue statistics.
  ///
  [[nodiscard]] CogQueueStats stats() const override;

private:
  std::pmr::list<CogEnvelope> queue_;
};

} // namespace clockwork
