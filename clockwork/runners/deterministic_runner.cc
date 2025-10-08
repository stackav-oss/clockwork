// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/runners/deterministic_runner.hh"

#include "clockwork/common/cog_envelope.hh"
#include "jewels/time/conversions.hh"
#include "jewels/time/sync_time.hh"

#include <fmt10/format.h>

#include <algorithm>
#include <chrono>
#include <compare>
#include <cstdint>
#include <iostream>
#include <string>
#include <tuple>
#include <typeinfo>
#include <utility>

namespace clockwork
{

DeterministicRunner::DeterministicRunner(DeterministicRunnerConfig config)
  : config_(std::move(config)), timers_(config_.resource)
{
  queue_ = std::dynamic_pointer_cast<DeterministicCogQueue>(config_.queue);
  if (!queue_)
  {
    throw std::bad_cast();
  }

  for (auto& abstract_timer : config_.timers)
  {
    auto timer = std::dynamic_pointer_cast<DeterministicTimer>(abstract_timer);
    if (!timer)
    {
      throw std::bad_cast();
    }
    timers_.emplace_back(timer);
  }
}

jewels::expected<std::optional<jewels::time::SyncTime>, jewels::MonoError> DeterministicRunner::initialize()
{
  if (config_.channel_publisher)
  {
    auto publisher_status = config_.channel_publisher->initialize();
    if (!publisher_status)
    {
      return jewels::unexpected(jewels::MonoError{});
    }
    return config_.channel_publisher->try_next_message_time();
  }
  return std::nullopt;
}

void DeterministicRunner::update_time(const jewels::time::SyncTime& new_time)
{
  const auto current_wall_time = jewels::time::SteadyClock::now();
  // Print at 1 Hz
  if (
    static_cast<uint32_t>(
      std::chrono::duration_cast<std::chrono::seconds>(current_wall_time - wall_update_time_).count()) >= 1)
  {
    std::cerr << fmt::format(
                   "INFO Runner Progress: sim elapsed: ({:4.1f} / {:4.1f} s) wall elapsed: ({:3d} s) sim "
                   "time: ({} ns)\n",
                   jewels::time::to_seconds<float>(new_time - start_time_),
                   jewels::time::to_seconds<float>(end_time_ - start_time_),
                   std::chrono::duration_cast<std::chrono::seconds>(current_wall_time - wall_start_time_).count(),
                   jewels::time::get_ns(new_time))
              << std::flush;

    wall_update_time_ = current_wall_time;
  }
  current_time_ = new_time;
}

void DeterministicRunner::start(
  jewels::time::SyncTime start_time, jewels::time::SyncTime end_time, jewels::cli::ExitCondition& exit)
{
  start_time_ = start_time;
  end_time_ = end_time;
  wall_start_time_ = jewels::time::SteadyClock::now();
  auto current_log_time_status = initialize();
  if (!current_log_time_status)
  {
    // This error gets logged in the call to initialize.
    return;
  }
  auto current_log_time = current_log_time_status.value();
  update_time(start_time);
  update_timers();
  while (current_time_ <= end_time)
  {
    if (exit.check())
    {
      break;
    }

    update_timers();
    auto next_result = queue_->peek();
    auto next_timer_time = get_next_timer_time();

    if (config_.channel_publisher)
    {
      current_log_time = config_.channel_publisher->try_next_message_time();
    }

    if (
      next_result && (!current_log_time || (next_result->ready_time < current_log_time)) &&
      (!next_timer_time || (next_result->ready_time < next_timer_time)))
    {
      // Pop the result from the queue *before* executing it since downstream cogs can be added onto the queue
      // after execute due to the outputs from the current cog.
      queue_->pop({}); // NOLINT(cert-err33-c) False positive

      update_time(next_result->ready_time);
      auto ready = next_result->cog->prepare_for_execution(next_result->ready_time);
      if (ready)
      {
        auto params =
          CogExecuteParams{.start_time = next_result->ready_time, .execution_mode = CogExecutionMode::deterministic};
        // The return value is ignored because all errors are already reported within execute() to the extent that we
        // can report them at present. Some of this reporting/handling is being improved in OI-2593, and more will be
        // done in OI-2730 when we have an improved observability framework.
        std::ignore = next_result->cog->execute(params); // TODO(OI-2730): Report errors from execute via observability
      }
    }
    else if (
      config_.channel_publisher && current_log_time && (!next_timer_time || (current_log_time < next_timer_time)))
    {
      update_time(*current_log_time);
      if (!config_.channel_publisher->publish_next_message())
      {
        break;
      }
    }
    else if (next_timer_time)
    {
      update_time(*next_timer_time);
      update_timers();
    }
    else
    {
      break;
    }
  }
}

std::optional<jewels::time::SyncTime> DeterministicRunner::get_next_timer_time() const
{
  std::optional<jewels::time::SyncTime> min_time;
  for (const auto& timer : timers_)
  {
    if (timer->started())
    {
      if (!min_time)
      {
        min_time.emplace(timer->next_event_time());
      }
      else
      {
        min_time.emplace(std::min(*min_time, timer->next_event_time()));
      }
    }
  }
  return min_time;
}

void DeterministicRunner::update_timers()
{
  for (auto& timer : timers_)
  {
    timer->update(current_time_);
  }
}

} // namespace clockwork
