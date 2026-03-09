// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/runners/online_cog_queue.hh"

#include "clockwork/common/abstract_cog.hh"
#include "clockwork/common/cog_envelope.hh"
#include "clockwork/common/cog_execution_error_clk_cc.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"

#include <algorithm>
#include <compare>
#include <utility>

namespace clockwork
{

OnlineCogQueue::OnlineCogQueue(jewels::memory::MemoryResource resource)
  : queue_(resource)
{
}

OnlineCogQueue::~OnlineCogQueue() = default;

void OnlineCogQueue::notify()
{
  condition_variable_.notify_all();
}

void OnlineCogQueue::push(CogEnvelope envelope)
{
  const std::scoped_lock lock(mutex_);
  if (std::ranges::none_of(queue_, [&](const auto& element) { return envelope.cog == element.cog; }))
  {
    queue_.push_back(envelope);
    condition_variable_.notify_one();
  }
}

auto OnlineCogQueue::pop(std::chrono::nanoseconds timeout) -> PopResult
{
  std::unique_lock lock(mutex_);
  const auto start_time = std::chrono::steady_clock::now();

  bool first_iter = true;
  while (first_iter || std::chrono::steady_clock::now() - start_time < timeout)
  {
    first_iter = false;

    for (auto it = queue_.begin(); it != queue_.end();)
    {
      auto prepare_result = it->cog->prepare_for_execution(jewels::time::SyncClock::now());
      if (prepare_result)
      {
        auto result = std::move(*it);
        queue_.erase(it);
        return result;
      }

      if (prepare_result.error() == CogExecutionError::not_ready)
      {
        it = queue_.erase(it);
      }
      else
      {
        ++it;
      }
    }

    condition_variable_.wait_for(lock, (timeout - (std::chrono::steady_clock::now() - start_time)));
  }

  return jewels::unexpected(jewels::MonoError{});
}

CogQueueStats OnlineCogQueue::stats() const
{
  const std::scoped_lock lock(mutex_);
  return CogQueueStats{.size = queue_.size()};
}

bool OnlineCogQueue::is_offline() const
{
  return false;
}

} // namespace clockwork
