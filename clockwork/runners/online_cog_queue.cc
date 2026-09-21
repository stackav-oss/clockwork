// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/runners/online_cog_queue.hh"

#include "clockwork/common/abstract_cog.hh"
#include "clockwork/common/cog_envelope.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"

#include <algorithm>
#include <compare>
#include <optional>
#include <string_view>
#include <utility>

namespace clockwork
{

OnlineCogQueue::OnlineCogQueue(jewels::memory::MemoryResource resource)
  : memory_resource_(resource), queue_(resource)
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

std::optional<CogEnvelope> OnlineCogQueue::try_pop_ready(
  jewels::Out<std::optional<jewels::time::SyncTime>> earliest_throttle_deadline,
  jewels::Out<TimerUpdates> timer_updates)
{
  const auto update_earliest_deadline = [&earliest_throttle_deadline](const jewels::time::SyncTime deadline)
  {
    if (!*earliest_throttle_deadline || deadline < **earliest_throttle_deadline)
    {
      *earliest_throttle_deadline = deadline;
    }
  };

  for (auto it = queue_.begin(); it != queue_.end();)
  {
    const auto current_time = jewels::time::SyncClock::now();
    if (current_time < it->throttled_until)
    {
      update_earliest_deadline(it->throttled_until);
      ++it;
      continue;
    }

    auto throttled_until = jewels::time::SyncTime::min();
    switch (it->cog->prepare_for_execution(jewels::Out{throttled_until}, current_time).get())
    {
    case CogPrepareResult::ready:
    {
      auto result = std::move(*it);
      queue_.erase(it);
      return result;
    }
    case CogPrepareResult::not_ready:
      it = queue_.erase(it);
      break;
    case CogPrepareResult::publisher_throttled:
      it->throttled_until = throttled_until;
      update_earliest_deadline(throttled_until);
      timer_updates->emplace_back(it->cog, throttled_until);
      ++it;
      break;
    case CogPrepareResult::states_lock_contention:
    case CogPrepareResult::reentry_lock_contention:
      ++it;
      break;
    }
  }
  return std::nullopt;
}

void OnlineCogQueue::arm_publisher_throttle_timers(const TimerUpdates& timer_updates)
{
  for (const auto& [cog, deadline] : timer_updates)
  {
    if (jewels::fails(cog->arm_publisher_throttle_timer(deadline)))
    {
      jewels::log_cerr_error("Failed to arm publisher-throttle timer for cog '{}'.", cog->get_name());
    }
  }
}

auto OnlineCogQueue::pop(std::chrono::nanoseconds timeout) -> PopResult
{
  std::unique_lock lock(mutex_);
  const auto start_time = std::chrono::steady_clock::now();

  TimerUpdates timer_updates{memory_resource_};
  bool first_iter = true;
  while (first_iter || std::chrono::steady_clock::now() - start_time < timeout)
  {
    first_iter = false;
    std::optional<jewels::time::SyncTime> earliest_throttle_deadline;
    timer_updates.clear();

    auto ready_cog = try_pop_ready(jewels::Out{earliest_throttle_deadline}, jewels::Out{timer_updates});

    lock.unlock();
    arm_publisher_throttle_timers(timer_updates);
    if (ready_cog)
    {
      return std::move(*ready_cog);
    }
    lock.lock();

    const auto caller_remaining = timeout - (std::chrono::steady_clock::now() - start_time);
    if (earliest_throttle_deadline)
    {
      const auto throttle_remaining = *earliest_throttle_deadline - jewels::time::SyncClock::now();
      condition_variable_.wait_for(lock, std::min(caller_remaining, throttle_remaining));
    }
    else
    {
      condition_variable_.wait_for(lock, caller_remaining);
    }
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
