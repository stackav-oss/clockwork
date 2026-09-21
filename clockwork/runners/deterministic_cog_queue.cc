// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/runners/deterministic_cog_queue.hh"

#include "jewels/std/expected.hh"

#include <algorithm>
#include <compare>
#include <functional>
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
  const auto existing = std::ranges::find(queue_, envelope.cog, &CogEnvelope::cog);
  if (existing != queue_.end())
  {
    // A notification can arrive earlier than an already queued notification when the log publisher reaches its
    // end-of-log marker while future cog work is still queued. Keep the earliest notification so that work triggered
    // by the marker is not stranded behind the runner's end time.
    existing->ready_time = std::min(existing->ready_time, envelope.ready_time);
    return;
  }

  auto ins_it = queue_.begin();
  while (ins_it != queue_.end() && ins_it->ready_time < envelope.ready_time)
  {
    ++ins_it;
  }
  queue_.insert(ins_it, std::move(envelope));
}

DeterministicCogQueue::PopResult DeterministicCogQueue::pop(std::chrono::nanoseconds /*timeout*/)
{
  const auto iter = find_next();
  if (iter != queue_.end())
  {
    auto out = std::move(*iter);
    queue_.erase(iter);
    return out;
  }
  return jewels::unexpected(jewels::MonoError{});
}

DeterministicCogQueue::PopResult DeterministicCogQueue::peek()
{
  const auto iter = find_next();
  if (iter == queue_.end())
  {
    return jewels::unexpected(jewels::MonoError{});
  }
  auto out = *iter;
  out.ready_time = std::max(out.ready_time, out.throttled_until);
  return out;
}

void DeterministicCogQueue::remove_next()
{
  const auto iter = find_next();
  if (iter != queue_.end())
  {
    queue_.erase(iter);
  }
}

void DeterministicCogQueue::set_throttled_until(
  const jewels::memory::ObjectPtr<AbstractCog> cog, const jewels::time::SyncTime throttled_until)
{
  const auto iter = std::ranges::find(queue_, cog, &CogEnvelope::cog);
  if (iter != queue_.end())
  {
    iter->throttled_until = throttled_until;
  }
}

DeterministicCogQueue::QueueIterator DeterministicCogQueue::find_next()
{
  return std::ranges::min_element(
    queue_, {}, [](const CogEnvelope& envelope) { return std::max(envelope.ready_time, envelope.throttled_until); });
}

CogQueueStats DeterministicCogQueue::stats() const
{
  return CogQueueStats{.size = queue_.size()};
}

bool DeterministicCogQueue::is_offline() const
{
  return true;
}

} // namespace clockwork
