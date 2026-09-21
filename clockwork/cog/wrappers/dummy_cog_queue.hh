// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/abstract_cog_queue.hh"

#include <chrono>

namespace clockwork::testing
{

///
/// Dummy cog queue used by the generated unit test cog wrappers
///
class DummyCogQueue : public AbstractCogQueue
{
public:
  DummyCogQueue() noexcept = default;
  ~DummyCogQueue() override = default;
  DummyCogQueue(const DummyCogQueue&) = delete;
  DummyCogQueue& operator=(const DummyCogQueue&) = delete;
  DummyCogQueue(DummyCogQueue&&) = delete;
  DummyCogQueue& operator=(DummyCogQueue&&) = delete;

  /// @see AbstractCogQueue::notify
  void notify() override;

  /// @see AbstractCogQueue::push
  void push(CogEnvelope envelope) override;

  /// @see AbstractCogQueue::pop
  PopResult pop(std::chrono::nanoseconds timeout) override;

  /// @see AbstractCogQueue::stats
  [[nodiscard]] CogQueueStats stats() const override;

  /// @see AbstractCogQueue::is_offline
  [[nodiscard]] bool is_offline() const override;
};

} // namespace clockwork::testing
