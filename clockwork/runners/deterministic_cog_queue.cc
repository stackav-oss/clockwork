// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/runners/deterministic_cog_queue.hh"

#include "jewels/std/expected.hh"

#include <compare>
#include <utility>

namespace clockwork
{

DeterministicCogQueue::DeterministicCogQueue(jewels::memory::MemoryResource resource)
  : queue_(resource)
{
}

void DeterministicCogQueue::notify()
{
  // Do nothing.
}

void DeterministicCogQueue::push(CogEnvelope envelope)
{
  auto ins_it = queue_.begin();
  while (ins_it != queue_.end() && ins_it->ready_time < envelope.ready_time)
  {
    ++ins_it;
  }
  queue_.insert(ins_it, std::move(envelope));
}

DeterministicCogQueue::PopResult DeterministicCogQueue::pop(std::chrono::nanoseconds /*timeout*/)
{
  if (!queue_.empty())
  {
    auto out = queue_.front();
    queue_.pop_front();
    return out;
  }
  return jewels::unexpected(jewels::MonoError{});
}

DeterministicCogQueue::PopResult DeterministicCogQueue::peek()
{
  if (!queue_.empty())
  {
    auto out = queue_.front();
    return out;
  }
  return jewels::unexpected(jewels::MonoError{});
}

CogQueueStats DeterministicCogQueue::stats() const
{
  return CogQueueStats{.size = queue_.size()};
}

} // namespace clockwork
