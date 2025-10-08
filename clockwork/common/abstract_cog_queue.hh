// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/forward.hh"
#include "jewels/std/expected.hh"

#include <chrono>
#include <cstddef>

namespace clockwork
{

///
/// Cog Queue Statistics
///
struct CogQueueStats
{
  ///
  /// Current size of the queue.
  ///
  size_t size = {};
};

///
/// Virtual Cog Queue Interface.
///
/// This interface is used by the runner thread pool to execute
/// Cogs as they be come ready. Each queue implementation must be
/// thread safe.
///
class AbstractCogQueue
{
public:
  using PopResult = jewels::expected<CogEnvelope, jewels::MonoError>;

  ///
  /// Constructor.
  ///
  AbstractCogQueue();

  ///
  /// Destructor.
  ///
  virtual ~AbstractCogQueue();

  AbstractCogQueue(const AbstractCogQueue&) = delete;
  AbstractCogQueue& operator=(const AbstractCogQueue&) = delete;
  AbstractCogQueue(AbstractCogQueue&&) = delete;
  AbstractCogQueue& operator=(AbstractCogQueue&&) = delete;

  ///
  /// Notify the queue that the elements on the queue changed.
  /// This is primarily used when cogs complete execution to allow the
  /// queues to re-check cogs on the queue for updated readiness (i.e. shared state
  /// is now available).
  ///
  virtual void notify() = 0;

  ///
  /// Push a ready Cog onto the queue.
  ///
  virtual void push(CogEnvelope envelope) = 0;

  ///
  /// Pop the next available Cog off of the queue.
  /// @param[in] timeout Max time to wait for a new element.
  /// @returns The next element in the queue or unexpected on timeout.
  ///
  virtual PopResult pop(std::chrono::nanoseconds timeout) = 0;

  ///
  /// Get the number of elements currently on the queue.
  /// @returns Queue statistics
  ///
  [[nodiscard]] virtual CogQueueStats stats() const = 0;

  ///
  /// Check if this cog queue is offline.
  /// @returns true if this cog queue is running offline.
  ///
  [[nodiscard]] virtual bool is_offline() const = 0;
};

} // namespace clockwork
